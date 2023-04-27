#pragma once

#include "config.h"
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

#include "../view-impl.hpp"
#include "xwayland-helpers.hpp"

#if WF_HAS_XWAYLAND

class wayfire_xwayland_view_base : public wf::wlr_view_t
{
  protected:
    wf::wl_listener_wrapper on_destroy, on_unmap, on_map, on_configure,
        on_set_title, on_set_app_id, on_or_changed, on_set_decorations,
        on_ping_timeout, on_set_window_type;

    wlr_xwayland_surface *xw;
    /** The geometry requested by the client */
    bool self_positioned = false;

    wf::signal::connection_t<wf::output_configuration_changed_signal> output_geometry_changed =
        [=] (wf::output_configuration_changed_signal *ev)
    {
        if (is_mapped())
        {
            auto wm_geometry = get_wm_geometry();
            move(wm_geometry.x, wm_geometry.y);
        }
    };

    bool has_type(xcb_atom_t type)
    {
        for (size_t i = 0; i < xw->window_type_len; i++)
        {
            if (xw->window_type[i] == type)
            {
                return true;
            }
        }

        return false;
    }

    bool is_dialog()
    {
        if (has_type(wf::xw::_NET_WM_WINDOW_TYPE_DIALOG) ||
            (xw->parent && (xw->window_type_len == 0)))
        {
            return true;
        } else
        {
            return false;
        }
    }

    /**
     * Determine whether the view should be treated as override-redirect or not.
     */
    bool is_unmanaged()
    {
        if (xw->override_redirect)
        {
            return true;
        }

        /** Example: Android Studio dialogs */
        if (xw->parent && !this->is_dialog() &&
            !this->has_type(wf::xw::_NET_WM_WINDOW_TYPE_NORMAL))
        {
            return true;
        }

        return false;
    }

    /**
     * Determine whether the view should be treated as a drag icon.
     */
    bool is_dnd()
    {
        return this->has_type(wf::xw::_NET_WM_WINDOW_TYPE_DND);
    }

    /**
     * Get the current implementation type.
     */
    virtual wf::xw::view_type get_current_impl_type() const = 0;

  public:
    wayfire_xwayland_view_base(wlr_xwayland_surface *xww) :
        wlr_view_t(), xw(xww)
    {}

    virtual void initialize() override
    {
        wf::wlr_view_t::initialize();

        // Set the output early, so that we can emit the signals on the output
        if (!get_output())
        {
            set_output(wf::get_core().get_active_output());
        }

        on_map.set_callback([&] (void*) { map(xw->surface); });
        on_unmap.set_callback([&] (void*) { unmap(); });
        on_destroy.set_callback([&] (void*) { destroy(); });
        on_configure.set_callback([&] (void *data)
        {
            auto ev = static_cast<wlr_xwayland_surface_configure_event*>(data);
            wf::point_t output_origin = {0, 0};
            if (get_output())
            {
                output_origin = {
                    get_output()->get_relative_geometry().x,
                    get_output()->get_relative_geometry().y
                };
            }

            if (!is_mapped())
            {
                /* If the view is not mapped yet, let it be configured as it
                 * wishes. We will position it properly in ::map() */
                wlr_xwayland_surface_configure(xw,
                    ev->x, ev->y, ev->width, ev->height);

                if ((ev->mask & XCB_CONFIG_WINDOW_X) &&
                    (ev->mask & XCB_CONFIG_WINDOW_Y))
                {
                    this->self_positioned = true;
                    this->geometry.x = ev->x - output_origin.x;
                    this->geometry.y = ev->y - output_origin.y;
                }

                return;
            }

            /**
             * Regular Xwayland windows are not allowed to change their position
             * after mapping, in which respect they behave just like Wayland apps.
             *
             * However, OR views or special views which do not have NORMAL type
             * should be allowed to move around the screen.
             */
            bool enable_custom_position = xw->override_redirect ||
                (xw->window_type_len > 0 &&
                    xw->window_type[0] != wf::xw::_NET_WM_WINDOW_TYPE_NORMAL);

            if ((ev->mask & XCB_CONFIG_WINDOW_X) &&
                (ev->mask & XCB_CONFIG_WINDOW_Y) &&
                enable_custom_position)
            {
                /* override-redirect views generally have full freedom. */
                self_positioned = true;
                configure_request({ev->x, ev->y, ev->width, ev->height});

                return;
            }

            /* Use old x/y values */
            ev->x = geometry.x + output_origin.x;
            ev->y = geometry.y + output_origin.y;
            configure_request(wlr_box{ev->x, ev->y, ev->width, ev->height});
        });
        on_set_title.set_callback([&] (void*)
        {
            handle_title_changed(nonull(xw->title));
        });
        on_set_app_id.set_callback([&] (void*)
        {
            handle_app_id_changed(nonull(xw->class_t));
        });
        on_or_changed.set_callback([&] (void*)
        {
            recreate_view();
        });
        on_set_decorations.set_callback([&] (void*)
        {
            update_decorated();
        });
        on_ping_timeout.set_callback([&] (void*)
        {
            wf::emit_ping_timeout_signal(self());
        });
        on_set_window_type.set_callback([&] (void*)
        {
            recreate_view();
        });

        handle_title_changed(nonull(xw->title));
        handle_app_id_changed(nonull(xw->class_t));
        update_decorated();

        on_map.connect(&xw->events.map);
        on_unmap.connect(&xw->events.unmap);
        on_destroy.connect(&xw->events.destroy);
        on_configure.connect(&xw->events.request_configure);
        on_set_title.connect(&xw->events.set_title);
        on_set_app_id.connect(&xw->events.set_class);
        on_or_changed.connect(&xw->events.set_override_redirect);
        on_ping_timeout.connect(&xw->events.ping_timeout);
        on_set_decorations.connect(&xw->events.set_decorations);
        on_set_window_type.connect(&xw->events.set_window_type);
    }

    /**
     * Destroy the view, and create a new one with the correct type -
     * unmanaged(override-redirect), DnD or normal.
     *
     * No-op if the view already has the correct type.
     */
    virtual void recreate_view();

    virtual void destroy() override
    {
        this->xw = nullptr;
        output_geometry_changed.disconnect();

        on_map.disconnect();
        on_unmap.disconnect();
        on_destroy.disconnect();
        on_configure.disconnect();
        on_set_title.disconnect();
        on_set_app_id.disconnect();
        on_or_changed.disconnect();
        on_ping_timeout.disconnect();
        on_set_decorations.disconnect();
        on_set_window_type.disconnect();

        wf::wlr_view_t::destroy();
    }

    virtual void ping() override
    {
        if (xw)
        {
            wlr_xwayland_surface_ping(xw);
        }
    }

    virtual bool should_be_decorated() override
    {
        return (wf::wlr_view_t::should_be_decorated() &&
            !has_type(wf::xw::_NET_WM_WINDOW_TYPE_SPLASH));
    }

    /* Translates geometry from X client configure requests to wayfire
     * coordinate system. The X coordinate system treats all outputs
     * as one big desktop, whereas wayfire treats the current workspace
     * of an output as 0,0 and everything else relative to that. This
     * means that we must take care when placing xwayland clients that
     * request a configure after initial mapping, while not on the
     * current workspace.
     *
     * @param output    The view's output
     * @param ws_offset The view's workspace minus the current workspace
     * @param geometry  The configure geometry as requested by the client
     *
     * @return Geometry with a position that is within the view's workarea.
     * The workarea is the workspace where the view was initially mapped.
     * Newly mapped views are placed on the current workspace.
     */
    wf::geometry_t translate_geometry_to_output(wf::output_t *output,
        wf::point_t ws_offset,
        wf::geometry_t g)
    {
        auto outputs = wf::get_core().output_layout->get_outputs();
        auto og   = output->get_layout_geometry();
        auto from = wf::get_core().output_layout->get_output_at(
            g.x + g.width / 2 + og.x, g.y + g.height / 2 + og.y);
        if (!from)
        {
            return g;
        }

        auto lg = from->get_layout_geometry();
        g.x += (og.x - lg.x) + ws_offset.x * og.width;
        g.y += (og.y - lg.y) + ws_offset.y * og.height;
        if (!this->is_mapped())
        {
            g.x *= (float)og.width / lg.width;
            g.y *= (float)og.height / lg.height;
        }

        return g;
    }

    virtual void configure_request(wf::geometry_t configure_geometry)
    {
        /* Wayfire positions views relative to their output, but Xwayland
         * windows have a global positioning. So, we need to make sure that we
         * always transform between output-local coordinates and global
         * coordinates. Additionally, when clients send a configure request
         * after they have already been mapped, we keep the view on the
         * workspace where its center point was from last configure, in
         * case the current workspace is not where the view lives */
        auto o = get_output();
        if (o)
        {
            auto view_workarea = (fullscreen ?
                o->get_relative_geometry() : o->workspace->get_workarea());
            auto og = o->get_layout_geometry();
            configure_geometry.x -= og.x;
            configure_geometry.y -= og.y;

            auto view = this->self();
            while (view->parent)
            {
                view = view->parent;
            }

            auto vg = view->get_wm_geometry();

            // View workspace relative to current workspace
            wf::point_t view_ws = {0, 0};
            if (view->is_mapped())
            {
                view_ws = {
                    (int)std::floor((vg.x + vg.width / 2.0) / og.width),
                    (int)std::floor((vg.y + vg.height / 2.0) / og.height),
                };

                view_workarea.x += og.width * view_ws.x;
                view_workarea.y += og.height * view_ws.y;
            }

            configure_geometry = translate_geometry_to_output(
                o, view_ws, configure_geometry);
            configure_geometry = wf::clamp(configure_geometry, view_workarea);
        }

        if (priv->frame)
        {
            configure_geometry =
                priv->frame->expand_wm_geometry(configure_geometry);
        }

        set_geometry(configure_geometry);
    }

    void update_decorated()
    {
        uint32_t csd_flags = WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE |
            WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER;
        this->set_decoration_mode(xw->decorations & csd_flags);
    }

    virtual void close() override
    {
        if (xw)
        {
            wlr_xwayland_surface_close(xw);
        }

        wf::wlr_view_t::close();
    }

    void set_activated(bool active) override
    {
        if (xw)
        {
            wlr_xwayland_surface_activate(xw, active);
        }

        wf::wlr_view_t::set_activated(active);
    }

    void set_geometry(wf::geometry_t geometry) override
    {
        wlr_view_t::move(geometry.x, geometry.y);
        resize(geometry.width, geometry.height);
    }

    void send_configure(int width, int height)
    {
        if (!xw)
        {
            return;
        }

        if ((width < 0) || (height < 0))
        {
            /* such a configure request would freeze xwayland.
             * This is most probably a bug somewhere in the compositor. */
            LOGE("Configuring a xwayland surface with width/height <0");

            return;
        }

        auto output_geometry = get_output_geometry();

        int configure_x = output_geometry.x;
        int configure_y = output_geometry.y;

        if (get_output())
        {
            auto real_output = get_output()->get_layout_geometry();
            configure_x += real_output.x;
            configure_y += real_output.y;
        }

        wlr_xwayland_surface_configure(xw,
            configure_x, configure_y, width, height);
    }

    void send_configure()
    {
        send_configure(last_size_request.width, last_size_request.height);
    }

    void move(int x, int y) override
    {
        wf::wlr_view_t::move(x, y);
        if (!priv->in_continuous_move)
        {
            send_configure();
        }
    }

    virtual void set_output(wf::output_t *wo) override
    {
        output_geometry_changed.disconnect();
        wlr_view_t::set_output(wo);

        if (wo)
        {
            wo->connect(&output_geometry_changed);
        }

        /* Update the real position */
        if (is_mapped())
        {
            send_configure();
        }
    }
};

#endif
