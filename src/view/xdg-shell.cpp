#include <wayfire/util/log.hpp>
#include <wayfire/debug.hpp>
#include "wayfire/core.hpp"
#include "surface-impl.hpp"
#include "wayfire/output.hpp"
#include "wayfire/decorator.hpp"
#include "xdg-shell.hpp"
#include "wayfire/output-layout.hpp"
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

wayfire_xdg_popup::wayfire_xdg_popup(wlr_xdg_popup *popup) :
    wf::wlr_view_t()
{
    this->popup_parent =
        dynamic_cast<wlr_view_t*>(wf::wf_surface_from_void(popup->parent->data));
    this->popup = popup;
    this->role  = wf::VIEW_ROLE_UNMANAGED;
    this->view_impl->keyboard_focus_enabled = false;
    this->set_output(popup_parent->get_output());
}

void wayfire_xdg_popup::initialize()
{
    LOGI("New xdg popup");
    on_map.set_callback([&] (void*) { map(this->popup->base->surface); });
    on_unmap.set_callback([&] (void*)
    {
        pending_close.disconnect();
        unmap();
    });
    on_destroy.set_callback([&] (void*) { destroy(); });
    on_new_popup.set_callback([&] (void *data)
    {
        create_xdg_popup((wlr_xdg_popup*)data);
    });
    on_ping_timeout.set_callback([&] (void*)
    {
        wf::emit_ping_timeout_signal(self());
    });

    on_map.connect(&popup->base->events.map);
    on_unmap.connect(&popup->base->events.unmap);
    on_destroy.connect(&popup->base->events.destroy);
    on_new_popup.connect(&popup->base->events.new_popup);
    on_ping_timeout.connect(&popup->base->events.ping_timeout);

    popup->base->data = this;
    parent_geometry_changed.set_callback([=] (wf::signal_data_t*)
    {
        this->update_position();
    });
    parent_app_id_changed.set_callback([=] (wf::signal_data_t*)
    {
        this->handle_app_id_changed(popup_parent->get_app_id());
    });
    parent_title_changed.set_callback([=] (wf::signal_data_t*)
    {
        this->handle_title_changed(popup_parent->get_title());
    });

    popup_parent->connect_signal("geometry-changed",
        &this->parent_geometry_changed);
    popup_parent->connect_signal("app-id-changed",
        &this->parent_app_id_changed);
    popup_parent->connect_signal("title-changed",
        &this->parent_title_changed);

    unconstrain();
}

void wayfire_xdg_popup::map(wlr_surface *surface)
{
    uint32_t parent_layer =
        get_output()->workspace->get_view_layer(popup_parent->self());

    wf::layer_t target_layer = wf::LAYER_UNMANAGED;
    if (parent_layer > wf::LAYER_WORKSPACE)
    {
        target_layer = (wf::layer_t)parent_layer;
    }

    get_output()->workspace->add_view(self(), target_layer);

    wlr_view_t::map(surface);
    update_position();
}

void wayfire_xdg_popup::commit()
{
    wlr_view_t::commit();
    update_position();
}

void wayfire_xdg_popup::update_position()
{
    if (!popup_parent->is_mapped() || !is_mapped())
    {
        return;
    }

    wf::pointf_t popup_offset = {
        1.0 * popup->current.geometry.x + popup_parent->get_window_offset().x,
        1.0 * popup->current.geometry.y + popup_parent->get_window_offset().y,
    };

    auto parent_geometry = popup_parent->get_output_geometry();
    popup_offset.x += parent_geometry.x - get_window_offset().x;
    popup_offset.y += parent_geometry.y - get_window_offset().y;

    popup_offset = popup_parent->transform_point(popup_offset);
    this->move(popup_offset.x, popup_offset.y);
}

void wayfire_xdg_popup::unconstrain()
{
    wf::view_interface_t *toplevel_parent = this;
    while (true)
    {
        auto as_popup = dynamic_cast<wayfire_xdg_popup*>(toplevel_parent);
        if (as_popup)
        {
            toplevel_parent = as_popup->popup_parent;
        } else
        {
            break;
        }
    }

    if (!get_output() || !toplevel_parent)
    {
        return;
    }

    auto box = get_output()->get_relative_geometry();
    auto wm  = toplevel_parent->get_output_geometry();
    box.x -= wm.x;
    box.y -= wm.y;

    wlr_xdg_popup_unconstrain_from_box(popup, &box);
}

void wayfire_xdg_popup::destroy()
{
    on_map.disconnect();
    on_unmap.disconnect();
    on_destroy.disconnect();
    on_new_popup.disconnect();
    on_ping_timeout.disconnect();

    wlr_view_t::destroy();
}

wf::point_t wayfire_xdg_popup::get_window_offset()
{
    return {
        popup->base->current.geometry.x,
        popup->base->current.geometry.y,
    };
}

void wayfire_xdg_popup::close()
{
    pending_close.run_once([=] ()
    {
        if (is_mapped())
        {
            wlr_xdg_popup_destroy(popup);
        }
    });
}

void wayfire_xdg_popup::ping()
{
    if (popup)
    {
        wlr_xdg_surface_ping(popup->base);
    }
}

void create_xdg_popup(wlr_xdg_popup *popup)
{
    auto parent = wf::wf_surface_from_void(popup->parent->data);
    if (!parent)
    {
        LOGE("attempting to create a popup with unknown parent");

        return;
    }

    wf::get_core().add_view(std::make_unique<wayfire_xdg_popup>(popup));
}

wayfire_xdg_view::wayfire_xdg_view(wlr_xdg_toplevel *top) :
    wf::wlr_view_t(), xdg_toplevel(top)
{}

void wayfire_xdg_view::initialize()
{
    wlr_view_t::initialize();
    LOGI("new xdg_shell_stable surface: ", xdg_toplevel->title,
        " app-id: ", xdg_toplevel->app_id);

    handle_title_changed(nonull(xdg_toplevel->title));
    handle_app_id_changed(nonull(xdg_toplevel->app_id));

    on_map.set_callback([&] (void*) { map(xdg_toplevel->base->surface); });
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
        output->emit_signal("view-show-window-menu", &d);
        wf::get_core().emit_signal("view-show-window-menu", &d);
    });
    on_set_parent.set_callback([&] (void*)
    {
        auto parent = xdg_toplevel->parent ?
            wf::wf_view_from_void(
            xdg_toplevel->parent->base->data)->self() : nullptr;
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

wayfire_xdg_view::~wayfire_xdg_view()
{}

wf::geometry_t get_xdg_geometry(wlr_xdg_toplevel *toplevel)
{
    wlr_box xdg_geometry;
    wlr_xdg_surface_get_geometry(toplevel->base, &xdg_geometry);

    return xdg_geometry;
}

void wayfire_xdg_view::map(wlr_surface *surface)
{
    wlr_view_t::map(surface);
    create_toplevel();
}

void wayfire_xdg_view::commit()
{
    wlr_view_t::commit();

    /* On each commit, check whether the window geometry of the xdg_surface
     * changed. In those cases, we need to adjust the view's output geometry,
     * so that the apparent wm geometry doesn't change */
    auto wm    = get_wm_geometry();
    auto xdg_g = get_xdg_geometry(xdg_toplevel);
    if ((xdg_g.x != xdg_surface_offset.x) || (xdg_g.y != xdg_surface_offset.y))
    {
        xdg_surface_offset = {xdg_g.x, xdg_g.y};
        /* Note that we just changed the xdg_surface offset, which means we
         * also changed the wm geometry. Plugins which depend on the
         * geometry-changed signal however need to receive the appropriate
         * old geometry */
        set_position(wm.x, wm.y, wm, true);
    }

    if (xdg_toplevel->base->current.configure_serial == this->last_configure_serial)
    {
        this->last_size_request = wf::dimensions(xdg_g);
    }
}

wf::point_t wayfire_xdg_view::get_window_offset()
{
    return xdg_surface_offset;
}

wf::geometry_t wayfire_xdg_view::get_wm_geometry()
{
    if (!is_mapped())
    {
        return get_output_geometry();
    }

    auto output_g     = get_output_geometry();
    auto xdg_geometry = get_xdg_geometry(xdg_toplevel);

    wf::geometry_t wm = {
        .x     = output_g.x + xdg_surface_offset.x,
        .y     = output_g.y + xdg_surface_offset.y,
        .width = xdg_geometry.width,
        .height = xdg_geometry.height
    };

    if (view_impl->frame)
    {
        wm = view_impl->frame->expand_wm_geometry(wm);
    }

    return wm;
}

void wayfire_xdg_view::set_activated(bool act)
{
    /* we don't send activated or deactivated for shell views,
     * they should always be active */
    if (this->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
    {
        act = true;
    }

    last_configure_serial =
        wlr_xdg_toplevel_set_activated(xdg_toplevel, act);
    wf::wlr_view_t::set_activated(act);
}

void wayfire_xdg_view::set_tiled(uint32_t edges)
{
    wlr_xdg_toplevel_set_tiled(xdg_toplevel, edges);
    last_configure_serial = wlr_xdg_toplevel_set_maximized(xdg_toplevel,
        (edges == wf::TILED_EDGES_ALL));
    wlr_view_t::set_tiled(edges);
}

void wayfire_xdg_view::set_fullscreen(bool full)
{
    wf::wlr_view_t::set_fullscreen(full);
    last_configure_serial =
        wlr_xdg_toplevel_set_fullscreen(xdg_toplevel, full);
}

void wayfire_xdg_view::resize(int w, int h)
{
    if (view_impl->frame)
    {
        view_impl->frame->calculate_resize_size(w, h);
    }

    auto current_geometry = get_xdg_geometry(xdg_toplevel);
    wf::dimensions_t current_size{current_geometry.width, current_geometry.height};
    if (should_resize_client({w, h}, current_size))
    {
        this->last_size_request = {w, h};
        last_configure_serial   =
            wlr_xdg_toplevel_set_size(xdg_toplevel, w, h);
    }
}

void wayfire_xdg_view::request_native_size()
{
    last_configure_serial =
        wlr_xdg_toplevel_set_size(xdg_toplevel, 0, 0);
}

void wayfire_xdg_view::close()
{
    if (xdg_toplevel)
    {
        wlr_xdg_toplevel_send_close(xdg_toplevel);
        wf::wlr_view_t::close();
    }
}

void wayfire_xdg_view::ping()
{
    if (xdg_toplevel)
    {
        wlr_xdg_surface_ping(xdg_toplevel->base);
    }
}

void wayfire_xdg_view::destroy()
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
    wf::wlr_view_t::destroy();
}

static wlr_xdg_shell *xdg_handle = nullptr;

void wf::init_xdg_shell()
{
    static wf::wl_listener_wrapper on_xdg_created;
    xdg_handle = wlr_xdg_shell_create(wf::get_core().display, 2);

    if (xdg_handle)
    {
        on_xdg_created.set_callback([&] (void *data)
        {
            auto surf = static_cast<wlr_xdg_surface*>(data);
            if (surf->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
            {
                wf::get_core().add_view(
                    std::make_unique<wayfire_xdg_view>(surf->toplevel));
            }
        });
        on_xdg_created.connect(&xdg_handle->events.new_surface);
    }
}
