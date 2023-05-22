#include "xdg-toplevel-view.hpp"
#include "wayfire/core.hpp"
#include <wayfire/txn/transaction.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include "../view-impl.hpp"
#include "../xdg-shell.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-manager.hpp>

/**
 * When we get a request for setting CSD, the view might not have been
 * created. So, we store all requests in core, and the views pick this
 * information when they are created
 */
std::unordered_map<wlr_surface*, uint32_t> uses_csd;

wf::xdg_toplevel_view_t::xdg_toplevel_view_t(wlr_xdg_toplevel *tlvl)
{
    this->xdg_toplevel = tlvl;
    this->main_surface = std::make_shared<scene::wlr_surface_node_t>(tlvl->base->surface, false);

    on_surface_commit.set_callback([&] (void*) { commit(); });
    on_map.set_callback([&] (void*) { map(); });
    on_unmap.set_callback([&] (void*) { unmap(); });
    on_destroy.set_callback([&] (void*) { destroy(); });
    on_new_popup.set_callback([&] (void *data)
    {
        create_xdg_popup((decltype(xdg_toplevel->base->popup))data);
    });

    on_set_title.set_callback([&] (void*)
    {
        handle_title_changed(nonull(xdg_toplevel->title));
    });
    on_set_app_id.set_callback([&] (void*)
    {
        handle_app_id_changed(nonull(xdg_toplevel->app_id));
    });
    on_show_window_menu.set_callback([&] (void *data)
    {
        wlr_xdg_toplevel_show_window_menu_event *event =
            (wlr_xdg_toplevel_show_window_menu_event*)data;
        auto view   = self();
        auto output = view->get_output();
        if (!output)
        {
            return;
        }

        wf::view_show_window_menu_signal d;
        d.view = view;
        d.relative_position.x = event->x;
        d.relative_position.y = event->y;
        output->emit(&d);
        wf::get_core().emit(&d);
    });
    on_set_parent.set_callback([&] (void*)
    {
        auto parent =
            xdg_toplevel->parent ? (wf::view_interface_t*)(xdg_toplevel->parent->base->data) : nullptr;
        set_toplevel_parent(parent);
    });
    on_ping_timeout.set_callback([&] (void*)
    {
        wf::emit_ping_timeout_signal(self());
    });

    on_request_move.set_callback([&] (void*) { move_request(); });
    on_request_resize.set_callback([&] (auto data)
    {
        auto ev = static_cast<wlr_xdg_toplevel_resize_event*>(data);
        resize_request(ev->edges);
    });
    on_request_minimize.set_callback([&] (void*) { minimize_request(true); });
    on_request_maximize.set_callback([&] (void *data)
    {
        tile_request(xdg_toplevel->requested.maximized ?
            wf::TILED_EDGES_ALL : 0);
    });
    on_request_fullscreen.set_callback([&] (void *data)
    {
        wlr_xdg_toplevel_requested *req = &xdg_toplevel->requested;
        auto wo = wf::get_core().output_layout->find_output(req->fullscreen_output);
        fullscreen_request(wo, req->fullscreen);
    });

    on_map.connect(&xdg_toplevel->base->events.map);
    on_unmap.connect(&xdg_toplevel->base->events.unmap);
    on_destroy.connect(&xdg_toplevel->base->events.destroy);
    on_new_popup.connect(&xdg_toplevel->base->events.new_popup);
    on_ping_timeout.connect(&xdg_toplevel->base->events.ping_timeout);

    on_set_title.connect(&xdg_toplevel->events.set_title);
    on_set_app_id.connect(&xdg_toplevel->events.set_app_id);
    on_set_parent.connect(&xdg_toplevel->events.set_parent);
    on_request_move.connect(&xdg_toplevel->events.request_move);
    on_request_resize.connect(&xdg_toplevel->events.request_resize);
    on_request_maximize.connect(&xdg_toplevel->events.request_maximize);
    on_request_minimize.connect(&xdg_toplevel->events.request_minimize);
    on_show_window_menu.connect(&xdg_toplevel->events.request_show_window_menu);
    on_request_fullscreen.connect(&xdg_toplevel->events.request_fullscreen);

    xdg_toplevel->base->data = dynamic_cast<view_interface_t*>(this);
}

void wf::xdg_toplevel_view_t::move(int x, int y)
{
    this->damage();

    view_geometry_changed_signal geometry_changed;
    geometry_changed.view = self();
    geometry_changed.old_geometry = this->wm_geometry;

    this->wm_geometry.x = x;
    this->wm_geometry.y = y;

    if (priv->frame)
    {
        auto expanded    = priv->frame->expand_wm_geometry(this->wm_geometry);
        auto deco_offset = wf::origin(wm_geometry) - wf::origin(expanded);

        this->base_geometry.x = this->wm_geometry.x - wm_offset.x + deco_offset.x;
        this->base_geometry.y = this->wm_geometry.y - wm_offset.y + deco_offset.y;
    } else
    {
        this->base_geometry.x = this->wm_geometry.x - wm_offset.x;
        this->base_geometry.y = this->wm_geometry.y - wm_offset.y;
    }

    /* Make sure that if we move the view while it is unmapped, its snapshot
     * is still valid coordinates */
    priv->offscreen_buffer = priv->offscreen_buffer.translated({
        x - geometry_changed.old_geometry.x, y - geometry_changed.old_geometry.y,
    });

    this->damage();

    emit(&geometry_changed);
    wf::get_core().emit(&geometry_changed);
    if (get_output())
    {
        get_output()->emit(&geometry_changed);
    }

    scene::update(this->get_surface_root_node(), scene::update_flag::GEOMETRY);
}

wf::geometry_t get_xdg_geometry(wlr_xdg_toplevel *toplevel)
{
    wlr_box xdg_geometry;
    wlr_xdg_surface_get_geometry(toplevel->base, &xdg_geometry);

    return xdg_geometry;
}

void wf::xdg_toplevel_view_t::resize(int w, int h)
{
    if (priv->frame)
    {
        priv->frame->calculate_resize_size(w, h);
    }

    auto current_geometry = get_xdg_geometry(xdg_toplevel);
    wf::dimensions_t current_size{current_geometry.width, current_geometry.height};
    if (should_resize_client({w, h}, current_size))
    {
        this->last_size_request = {w, h};
        last_configure_serial   = wlr_xdg_toplevel_set_size(xdg_toplevel, w, h);
    }
}

void wf::xdg_toplevel_view_t::request_native_size()
{
    last_configure_serial = wlr_xdg_toplevel_set_size(xdg_toplevel, 0, 0);
}

void wf::xdg_toplevel_view_t::close()
{
    if (xdg_toplevel)
    {
        wlr_xdg_toplevel_send_close(xdg_toplevel);
        view_interface_t::close();
    }
}

void wf::xdg_toplevel_view_t::ping()
{
    if (xdg_toplevel)
    {
        wlr_xdg_surface_ping(xdg_toplevel->base);
    }
}

wf::geometry_t wf::xdg_toplevel_view_t::get_wm_geometry()
{
    return wm_geometry;
}

wf::geometry_t wf::xdg_toplevel_view_t::get_output_geometry()
{
    return base_geometry;
}

wlr_surface*wf::xdg_toplevel_view_t::get_keyboard_focus_surface()
{
    if (is_mapped())
    {
        return xdg_toplevel->base->surface;
    }

    return nullptr;
}

bool wf::xdg_toplevel_view_t::is_focusable() const
{
    return true;
}

void wf::xdg_toplevel_view_t::set_tiled(uint32_t edges)
{
    wlr_xdg_toplevel_set_tiled(xdg_toplevel, edges);
    last_configure_serial = wlr_xdg_toplevel_set_maximized(xdg_toplevel,
        (edges == wf::TILED_EDGES_ALL));

    wf::view_interface_t::set_tiled(edges);
}

void wf::xdg_toplevel_view_t::set_fullscreen(bool fullscreen)
{
    view_interface_t::set_fullscreen(fullscreen);
    last_configure_serial = wlr_xdg_toplevel_set_fullscreen(xdg_toplevel, fullscreen);
}

void wf::xdg_toplevel_view_t::set_activated(bool active)
{
    view_interface_t::set_activated(active);
    last_configure_serial = wlr_xdg_toplevel_set_activated(xdg_toplevel, active);
}

std::string wf::xdg_toplevel_view_t::get_app_id()
{
    return app_id;
}

std::string wf::xdg_toplevel_view_t::get_title()
{
    return title;
}

bool wf::xdg_toplevel_view_t::should_be_decorated()
{
    return !has_client_decoration;
}

bool wf::xdg_toplevel_view_t::is_mapped() const
{
    return priv->wsurface;
}

void wf::xdg_toplevel_view_t::initialize()
{
    view_interface_t::initialize();

    // Set the output early, so that we can emit the signals on the output
    if (!get_output())
    {
        set_output(wf::get_core().get_active_output());
    }

    LOGI("new xdg_shell_stable surface: ", xdg_toplevel->title,
        " app-id: ", xdg_toplevel->app_id);

    handle_title_changed(nonull(xdg_toplevel->title));
    handle_app_id_changed(nonull(xdg_toplevel->app_id));

    // set initial parent
    on_set_parent.emit(nullptr);

    if (xdg_toplevel->requested.fullscreen)
    {
        fullscreen_request(get_output(), true);
    }

    if (xdg_toplevel->requested.maximized)
    {
        tile_request(wf::TILED_EDGES_ALL);
    }
}

bool wf::xdg_toplevel_view_t::should_resize_client(wf::dimensions_t old, wf::dimensions_t next)
{
    /*
     * Do not send a configure if the client will retain its size.
     * This is needed if a client starts with one size and immediately resizes
     * again.
     *
     * If we do configure it with the given size, then it will think that we
     * are requesting the given size, and won't resize itself again.
     */
    if (this->last_size_request == wf::dimensions_t{0, 0})
    {
        return old != next;
    } else
    {
        return old != last_size_request;
    }
}

void wf::xdg_toplevel_view_t::map()
{
    auto surf = xdg_toplevel->base->surface;
    if (uses_csd.count(surf))
    {
        this->has_client_decoration = uses_csd[surf];
    }

    priv->set_mapped_surface_contents(main_surface);
    priv->set_mapped(true);
    on_surface_commit.connect(&surf->events.commit);

    update_size();

    if (role == VIEW_ROLE_TOPLEVEL)
    {
        if (!parent)
        {
            get_output()->workspace->add_view(self(), wf::LAYER_WORKSPACE);
        }

        get_output()->focus_view(self(), true);
    }

    damage();
    emit_view_map();
    /* Might trigger repositioning */
    set_toplevel_parent(this->parent);

    wf::dump_scene();
}

void wf::xdg_toplevel_view_t::unmap()
{
    damage();
    emit_view_pre_unmap();
    set_decoration(nullptr);

    main_surface = nullptr;
    priv->unset_mapped_surface_contents();
    on_surface_commit.disconnect();

    emit_view_unmap();
    priv->set_mapped(false);
}

void wf::xdg_toplevel_view_t::update_size()
{
    if (!priv->wsurface)
    {
        return;
    }

    wf::dimensions_t current_size = {priv->wsurface->current.width, priv->wsurface->current.height};
    if ((current_size.width == base_geometry.width) &&
        (current_size.height == base_geometry.height))
    {
        return;
    }

    /* Damage current size */
    view_damage_raw(self(), base_geometry);
    adjust_anchored_edge(current_size);

    view_geometry_changed_signal geometry_changed;
    geometry_changed.view = self();
    geometry_changed.old_geometry = get_wm_geometry();

    base_geometry.width  = current_size.width;
    base_geometry.height = current_size.height;

    auto xdg_g = get_xdg_geometry(this->xdg_toplevel);
    if (priv->frame)
    {
        xdg_g = priv->frame->expand_wm_geometry(xdg_g);
    }

    wm_geometry.width  = xdg_g.width;
    wm_geometry.height = xdg_g.height;

    /* Damage new size */
    damage();

    emit(&geometry_changed);
    wf::get_core().emit(&geometry_changed);
    if (get_output())
    {
        get_output()->emit(&geometry_changed);
    }

    scene::update(this->get_surface_root_node(), scene::update_flag::GEOMETRY);
}

void wf::xdg_toplevel_view_t::commit()
{
    update_size();

    /* Clear the resize edges.
     * This is must be done here because if the user(or plugin) resizes too fast,
     * the shell client might still haven't configured the surface, and in this
     * case the next commit(here) needs to still have access to the gravity */
    if (!priv->in_continuous_resize)
    {
        priv->edges = 0;
    }

    /* On each commit, check whether the window geometry of the xdg_surface
     * changed. In those cases, we need to adjust the view's output geometry,
     * so that the apparent wm geometry doesn't change */
    auto xdg_g = get_xdg_geometry(xdg_toplevel);
    if (wm_offset != wf::origin(xdg_g))
    {
        wm_offset = {xdg_g.x, xdg_g.y};
        move(wm_geometry.x, wm_geometry.y);
    }

    if (xdg_toplevel->base->current.configure_serial == this->last_configure_serial)
    {
        this->last_size_request = wf::dimensions(xdg_g);
    }

    scene::surface_state_t cur_state;
    cur_state.merge_state(xdg_toplevel->base->surface);
    main_surface->apply_state(std::move(cur_state));
}

void wf::xdg_toplevel_view_t::destroy()
{
    on_map.disconnect();
    on_unmap.disconnect();
    on_destroy.disconnect();
    on_new_popup.disconnect();
    on_set_title.disconnect();
    on_set_app_id.disconnect();
    on_set_parent.disconnect();
    on_ping_timeout.disconnect();
    on_request_move.disconnect();
    on_request_resize.disconnect();
    on_request_maximize.disconnect();
    on_request_minimize.disconnect();
    on_show_window_menu.disconnect();
    on_request_fullscreen.disconnect();

    xdg_toplevel = nullptr;
    /* Drop the internal reference */
    unref();
}

void wf::xdg_toplevel_view_t::handle_title_changed(std::string new_title)
{
    this->title = new_title;
    view_title_changed_signal data;
    data.view = self();
    emit(&data);
}

void wf::xdg_toplevel_view_t::handle_app_id_changed(std::string new_app_id)
{
    this->app_id = new_app_id;
    view_app_id_changed_signal data;
    data.view = self();
    emit(&data);
}

void wf::xdg_toplevel_view_t::adjust_anchored_edge(wf::dimensions_t new_size)
{
    if (priv->edges)
    {
        wf::point_t offset = {0, 0};
        if (priv->edges & WLR_EDGE_LEFT)
        {
            offset.x = base_geometry.width - new_size.width;
        }

        if (priv->edges & WLR_EDGE_TOP)
        {
            offset.y = base_geometry.height - new_size.height;
        }

        this->base_geometry = this->base_geometry + offset;
        this->wm_geometry   = this->wm_geometry + offset;
        priv->offscreen_buffer = priv->offscreen_buffer.translated(offset);
    }
}

void wf::xdg_toplevel_view_t::set_decoration_mode(bool use_csd)
{
    bool was_decorated = should_be_decorated();
    this->has_client_decoration = use_csd;
    if ((was_decorated != should_be_decorated()) && is_mapped())
    {
        wf::view_decoration_state_updated_signal data;
        data.view = self();

        this->emit(&data);
        wf::get_core().emit(&data);
    }
}

/* decorations impl */
struct wf_server_decoration_t
{
    wlr_server_decoration *decor;
    wf::wl_listener_wrapper on_mode_set, on_destroy;

    std::function<void(void*)> mode_set = [&] (void*)
    {
        bool use_csd = decor->mode == WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
        uses_csd[decor->surface] = use_csd;

        auto view = dynamic_cast<wf::xdg_toplevel_view_t*>(
            wf::wl_surface_to_wayfire_view(decor->surface->resource).get());
        if (view)
        {
            view->set_decoration_mode(use_csd);
        }
    };

    wf_server_decoration_t(wlr_server_decoration *_decor) :
        decor(_decor)
    {
        on_mode_set.set_callback(mode_set);
        on_destroy.set_callback([&] (void*)
        {
            uses_csd.erase(decor->surface);
            delete this;
        });

        on_mode_set.connect(&decor->events.mode);
        on_destroy.connect(&decor->events.destroy);
        /* Read initial decoration settings */
        mode_set(NULL);
    }
};

struct wf_xdg_decoration_t
{
    wlr_xdg_toplevel_decoration_v1 *decor;
    wf::wl_listener_wrapper on_mode_request, on_commit, on_destroy;

    std::function<void(void*)> mode_request = [&] (void*)
    {
        wf::option_wrapper_t<std::string>
        deco_mode{"core/preferred_decoration_mode"};
        wlr_xdg_toplevel_decoration_v1_mode default_mode =
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        if ((std::string)deco_mode == "server")
        {
            default_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        }

        auto mode = decor->pending.mode;
        if (mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE)
        {
            mode = default_mode;
        }

        wlr_xdg_toplevel_decoration_v1_set_mode(decor, mode);
    };

    std::function<void(void*)> commit = [&] (void*)
    {
        bool use_csd = (decor->current.mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
        uses_csd[decor->surface->surface] = use_csd;

        auto wf_surface = dynamic_cast<wf::xdg_toplevel_view_t*>(
            wf::wl_surface_to_wayfire_view(decor->surface->surface->resource).get());
        if (wf_surface)
        {
            wf_surface->set_decoration_mode(use_csd);
        }
    };

    wf_xdg_decoration_t(wlr_xdg_toplevel_decoration_v1 *_decor) :
        decor(_decor)
    {
        on_mode_request.set_callback(mode_request);
        on_commit.set_callback(commit);
        on_destroy.set_callback([&] (void*)
        {
            uses_csd.erase(decor->surface->surface);
            delete this;
        });

        on_mode_request.connect(&decor->events.request_mode);
        on_commit.connect(&decor->surface->surface->events.commit);
        on_destroy.connect(&decor->events.destroy);
        /* Read initial decoration settings */
        mode_request(NULL);
    }
};

static void init_legacy_decoration()
{
    static wf::wl_listener_wrapper decoration_created;
    wf::option_wrapper_t<std::string> deco_mode{"core/preferred_decoration_mode"};
    uint32_t default_mode = WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
    if ((std::string)deco_mode == "server")
    {
        default_mode = WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
    }

    wlr_server_decoration_manager_set_default_mode(wf::get_core().protocols.decorator_manager, default_mode);

    decoration_created.set_callback([&] (void *data)
    {
        /* will be freed by the destroy request */
        new wf_server_decoration_t((wlr_server_decoration*)(data));
    });
    decoration_created.connect(&wf::get_core().protocols.decorator_manager->events.new_decoration);
}

static void init_xdg_decoration()
{
    static wf::wl_listener_wrapper xdg_decoration_created;
    xdg_decoration_created.set_callback([&] (void *data)
    {
        /* will be freed by the destroy request */
        new wf_xdg_decoration_t((wlr_xdg_toplevel_decoration_v1*)(data));
    });

    xdg_decoration_created.connect(&wf::get_core().protocols.xdg_decorator->events.new_toplevel_decoration);
}

void wf::init_xdg_decoration_handlers()
{
    init_legacy_decoration();
    init_xdg_decoration();
}
