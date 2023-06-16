#include "xdg-toplevel-view.hpp"
#include <wayfire/scene-operations.hpp>
#include "view/toplevel-node.hpp"
#include "wayfire/core.hpp"
#include <wayfire/txn/transaction.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include "../view-impl.hpp"
#include "../xdg-shell.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-set.hpp>
#include <wlr/util/edges.h>
#include <wayfire/window-manager.hpp>

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
    surface_root_node  = std::make_shared<toplevel_view_node_t>(this);
    this->set_surface_root_node(surface_root_node);

    this->on_toplevel_applied = [&] (xdg_toplevel_applied_state_signal *ev)
    {
        this->handle_toplevel_state_changed(ev->old_state);
    };
    wtoplevel = std::make_shared<xdg_toplevel_t>(tlvl, main_surface);
    wtoplevel->connect(&on_toplevel_applied);
    this->priv->toplevel = wtoplevel;

    on_map.set_callback([&] (void*)
    {
        wlr_box box;
        wlr_xdg_surface_get_geometry(xdg_toplevel->base, &box);
        wtoplevel->pending().mapped = true;
        wtoplevel->pending().geometry.width  = box.width;
        wtoplevel->pending().geometry.height = box.height;
        wf::get_core().tx_manager->schedule_object(wtoplevel);
    });
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
        set_toplevel_parent(toplevel_cast(parent));
    });
    on_ping_timeout.set_callback([&] (void*)
    {
        wf::emit_ping_timeout_signal(self());
    });

    on_request_move.set_callback([&] (void*)
    {
        wf::get_core().default_wm->move_request({this});
    });
    on_request_resize.set_callback([&] (auto data)
    {
        auto ev = static_cast<wlr_xdg_toplevel_resize_event*>(data);
        wf::get_core().default_wm->resize_request({this}, ev->edges);
    });
    on_request_minimize.set_callback([&] (void*)
    {
        wf::get_core().default_wm->minimize_request({this}, true);
    });
    on_request_maximize.set_callback([&] (void *data)
    {
        wf::get_core().default_wm->tile_request({this},
            xdg_toplevel->requested.maximized ? wf::TILED_EDGES_ALL : 0);
    });
    on_request_fullscreen.set_callback([&] (void *data)
    {
        wlr_xdg_toplevel_requested *req = &xdg_toplevel->requested;
        auto wo = wf::get_core().output_layout->find_output(req->fullscreen_output);
        wf::get_core().default_wm->fullscreen_request({this}, wo, req->fullscreen);
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
    this->wtoplevel->pending().geometry.x = x;
    this->wtoplevel->pending().geometry.y = y;
    wf::get_core().tx_manager->schedule_object(wtoplevel);
}

void wf::xdg_toplevel_view_t::resize(int w, int h)
{
    this->wtoplevel->pending().geometry.width  = w;
    this->wtoplevel->pending().geometry.height = h;
    wf::get_core().tx_manager->schedule_object(wtoplevel);
}

void wf::xdg_toplevel_view_t::set_geometry(wf::geometry_t g)
{
    this->wtoplevel->pending().geometry = g;
    wf::get_core().tx_manager->schedule_object(wtoplevel);
}

void wf::xdg_toplevel_view_t::request_native_size()
{
    this->wtoplevel->request_native_size();
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
    return wf::construct_box(wf::origin(wtoplevel->pending().geometry),
        wf::dimensions(wtoplevel->current().geometry));
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
    wlr_xdg_toplevel_set_maximized(xdg_toplevel, (edges == wf::TILED_EDGES_ALL));
    wf::toplevel_view_interface_t::set_tiled(edges);
}

void wf::xdg_toplevel_view_t::set_fullscreen(bool fullscreen)
{
    toplevel_view_interface_t::set_fullscreen(fullscreen);
    wlr_xdg_toplevel_set_fullscreen(xdg_toplevel, fullscreen);
}

void wf::xdg_toplevel_view_t::set_activated(bool active)
{
    toplevel_view_interface_t::set_activated(active);
    wlr_xdg_toplevel_set_activated(xdg_toplevel, active);
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
    return wtoplevel->current().mapped && priv->wsurface;
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
        wf::get_core().default_wm->fullscreen_request({this}, get_output(), true);
    }

    if (xdg_toplevel->requested.maximized)
    {
        wf::get_core().default_wm->tile_request({this}, TILED_EDGES_ALL);
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

    if (role == VIEW_ROLE_TOPLEVEL)
    {
        if (!parent)
        {
            wf::scene::readd_front(get_output()->wset()->get_node(), get_root_node());
            get_output()->wset()->add_view({this});
        }

        get_output()->focus_view(self(), true);
    }

    damage();
    emit_view_map();
    /* Might trigger repositioning */
    set_toplevel_parent(this->parent);
}

void wf::xdg_toplevel_view_t::unmap()
{
    damage();
    emit_view_pre_unmap();
    set_decoration(nullptr);

    main_surface = nullptr;
    priv->unset_mapped_surface_contents();

    emit_view_unmap();
    priv->set_mapped(false);
}

void wf::xdg_toplevel_view_t::handle_toplevel_state_changed(wf::toplevel_state_t old_state)
{
    surface_root_node->set_offset(wf::origin(wtoplevel->calculate_base_geometry()));
    if (!old_state.mapped && wtoplevel->current().mapped)
    {
        map();
    }

    wf::scene::damage_node(get_root_node(), last_bounding_box);
    wf::emit_geometry_changed_signal({this}, old_state.geometry);

    damage();
    last_bounding_box = this->get_surface_root_node()->get_bounding_box();
    scene::update(this->get_surface_root_node(), scene::update_flag::GEOMETRY);
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
}

void wf::xdg_toplevel_view_t::handle_app_id_changed(std::string new_app_id)
{
    this->app_id = new_app_id;
    emit_app_id_changed_signal(self());
}

void wf::xdg_toplevel_view_t::set_decoration_mode(bool use_csd)
{
    bool was_decorated = should_be_decorated();
    this->has_client_decoration = use_csd;
    if ((was_decorated != should_be_decorated()) && is_mapped())
    {
        wf::view_decoration_state_updated_signal data;
        data.view = {this};

        this->emit(&data);
        wf::get_core().emit(&data);
    }
}

void wf::xdg_toplevel_view_t::set_decoration(std::unique_ptr<decorator_frame_t_t> frame)
{
    wtoplevel->set_decoration(frame.get());
    wf::toplevel_view_interface_t::set_decoration(std::move(frame));
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
