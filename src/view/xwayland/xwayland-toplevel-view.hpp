#pragma once

#include "config.h"
#include "xwayland-view-base.hpp"

#if WF_HAS_XWAYLAND

class wayfire_xwayland_view : public wayfire_xwayland_view_base
{
    wf::wl_listener_wrapper on_request_move, on_request_resize,
        on_request_maximize, on_request_minimize, on_request_activate,
        on_request_fullscreen, on_set_parent, on_set_hints;

  public:
    wayfire_xwayland_view(wlr_xwayland_surface *xww) :
        wayfire_xwayland_view_base(xww)
    {}

    virtual void initialize() override
    {
        LOGE("new xwayland surface ", xw->title,
            " class: ", xw->class_t, " instance: ", xw->instance);
        wayfire_xwayland_view_base::initialize();

        on_request_move.set_callback([&] (void*) { move_request(); });
        on_request_resize.set_callback([&] (auto data)
        {
            auto ev = static_cast<wlr_xwayland_resize_event*>(data);
            resize_request(ev->edges);
        });
        on_request_activate.set_callback([&] (void*)
        {
            if (!this->activated)
            {
                wf::view_focus_request_signal data;
                data.view = self();
                data.self_request = true;
                emit(&data);
                wf::get_core().emit(&data);
            }
        });

        on_request_maximize.set_callback([&] (void*)
        {
            tile_request((xw->maximized_horz && xw->maximized_vert) ?
                wf::TILED_EDGES_ALL : 0);
        });
        on_request_fullscreen.set_callback([&] (void*)
        {
            fullscreen_request(get_output(), xw->fullscreen);
        });
        on_request_minimize.set_callback([&] (void *data)
        {
            auto ev = (wlr_xwayland_minimize_event*)data;
            minimize_request(ev->minimize);
        });

        on_set_parent.set_callback([&] (void*)
        {
            /* Menus, etc. with TRANSIENT_FOR but not dialogs */
            if (is_unmanaged())
            {
                recreate_view();

                return;
            }

            auto parent = xw->parent ? (wf::view_interface_t*)(xw->parent->data) : nullptr;

            // Make sure the parent is mapped, and that we are not a toplevel view
            if (parent)
            {
                if (!parent->is_mapped() ||
                    this->has_type(wf::xw::_NET_WM_WINDOW_TYPE_NORMAL))
                {
                    parent = nullptr;
                }
            }

            set_toplevel_parent(parent);
        });

        on_set_hints.set_callback([&] (void*)
        {
            wf::view_hints_changed_signal data;
            data.view = this;
            if (xw->hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY)
            {
                data.demands_attention = true;
            }

            wf::get_core().emit(&data);
            this->emit(&data);
        });
        on_set_parent.connect(&xw->events.set_parent);
        on_set_hints.connect(&xw->events.set_hints);

        on_request_move.connect(&xw->events.request_move);
        on_request_resize.connect(&xw->events.request_resize);
        on_request_activate.connect(&xw->events.request_activate);
        on_request_maximize.connect(&xw->events.request_maximize);
        on_request_minimize.connect(&xw->events.request_minimize);
        on_request_fullscreen.connect(&xw->events.request_fullscreen);

        xw->data = dynamic_cast<wf::view_interface_t*>(this);
        // set initial parent
        on_set_parent.emit(nullptr);
    }

    virtual void destroy() override
    {
        on_set_parent.disconnect();
        on_set_hints.disconnect();
        on_request_move.disconnect();
        on_request_resize.disconnect();
        on_request_activate.disconnect();
        on_request_maximize.disconnect();
        on_request_minimize.disconnect();
        on_request_fullscreen.disconnect();

        wayfire_xwayland_view_base::destroy();
    }

    void emit_view_map() override
    {
        /* Some X clients position themselves on map, and others let the window
         * manager determine this. We try to heuristically guess which of the
         * two cases we're dealing with by checking whether we have received
         * a valid ConfigureRequest before mapping */
        bool client_self_positioned = self_positioned;
        emit_view_map_signal(self(), client_self_positioned);
    }

    void map(wlr_surface *surface) override
    {
        priv->keyboard_focus_enabled =
            wlr_xwayland_or_surface_wants_focus(xw);

        if (xw->maximized_horz && xw->maximized_vert)
        {
            if ((xw->width > 0) && (xw->height > 0))
            {
                /* Save geometry which the window has put itself in */
                wf::geometry_t save_geometry = {
                    xw->x, xw->y, xw->width, xw->height
                };

                /* Make sure geometry is properly visible on the view output */
                save_geometry = wf::clamp(save_geometry,
                    get_output()->workspace->get_workarea());
                priv->update_windowed_geometry(self(), save_geometry);
            }

            tile_request(wf::TILED_EDGES_ALL);
        }

        if (xw->fullscreen)
        {
            fullscreen_request(get_output(), true);
        }

        if (!this->tiled_edges && !xw->fullscreen)
        {
            configure_request({xw->x, xw->y, xw->width, xw->height});
        }

        wf::wlr_view_t::map(surface);
    }

    void commit() override
    {
        if (!xw->has_alpha)
        {
            pixman_region32_union_rect(
                &priv->wsurface->opaque_region, &priv->wsurface->opaque_region,
                0, 0, priv->wsurface->current.width, priv->wsurface->current.height);
        }

        wf::wlr_view_t::commit();

        /* Avoid loops where the client wants to have a certain size but the
         * compositor keeps trying to resize it */
        last_size_request = wf::dimensions(geometry);
    }

    void set_moving(bool moving) override
    {
        wf::wlr_view_t::set_moving(moving);

        /* We don't send updates while in continuous move, because that means
         * too much configure requests. Instead, we set it at the end */
        if (!priv->in_continuous_move)
        {
            send_configure();
        }
    }

    void resize(int w, int h) override
    {
        if (priv->frame)
        {
            priv->frame->calculate_resize_size(w, h);
        }

        wf::dimensions_t current_size = {
            get_output_geometry().width,
            get_output_geometry().height
        };
        if (!should_resize_client({w, h}, current_size))
        {
            return;
        }

        this->last_size_request = {w, h};
        send_configure(w, h);
    }

    virtual void request_native_size() override
    {
        if (!is_mapped() || !xw->size_hints)
        {
            return;
        }

        if ((xw->size_hints->base_width > 0) && (xw->size_hints->base_height > 0))
        {
            this->last_size_request = {
                xw->size_hints->base_width,
                xw->size_hints->base_height
            };
            send_configure();
        }
    }

    void set_tiled(uint32_t edges) override
    {
        wf::wlr_view_t::set_tiled(edges);
        if (xw)
        {
            wlr_xwayland_surface_set_maximized(xw, !!edges);
        }
    }

    void set_fullscreen(bool full) override
    {
        wf::wlr_view_t::set_fullscreen(full);
        if (xw)
        {
            wlr_xwayland_surface_set_fullscreen(xw, full);
        }
    }

    void set_minimized(bool minimized) override
    {
        wf::wlr_view_t::set_minimized(minimized);
        if (xw)
        {
            wlr_xwayland_surface_set_minimized(xw, minimized);
        }
    }

    wf::xw::view_type get_current_impl_type() const override
    {
        return wf::xw::view_type::NORMAL;
    }
};

#endif
