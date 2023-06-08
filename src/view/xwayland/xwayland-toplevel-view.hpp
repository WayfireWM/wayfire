#pragma once

#include "config.h"
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/util.hpp"
#include "xwayland-view-base.hpp"
#include <wayfire/workarea.hpp>
#include "xwayland-toplevel.hpp"
#include <wayfire/txn/transaction-manager.hpp>

#if WF_HAS_XWAYLAND

class wayfire_xwayland_view : public wayfire_xwayland_view_base
{
    wf::wl_listener_wrapper on_request_move, on_request_resize,
        on_request_maximize, on_request_minimize, on_request_activate,
        on_request_fullscreen, on_set_parent, on_set_hints;

    wf::wl_listener_wrapper on_map;
    wf::wl_listener_wrapper on_unmap;

    std::shared_ptr<wf::xw::xwayland_toplevel_t> toplevel;

    /**
     * The bounding box of the view the last time it was rendered.
     *
     * This is used to damage the view when it is resized, because when a
     * transformer changes because the view is resized, we can't reliably
     * calculate the old view region to damage.
     */
    wf::geometry_t last_bounding_box{0, 0, 0, 0};

    wf::geometry_t get_output_geometry() override
    {
        return toplevel->calculate_base_geometry();
    }

    wf::geometry_t get_wm_geometry() override
    {
        return toplevel->current().geometry;
    }

    wf::signal::connection_t<wf::output_configuration_changed_signal> output_geometry_changed =
        [=] (wf::output_configuration_changed_signal *ev)
    {
        toplevel->set_output_offset(wf::origin(ev->output->get_layout_geometry()));
    };

    void handle_client_configure(wlr_xwayland_surface_configure_event *ev) override
    {
        wf::point_t output_origin = {0, 0};
        if (get_output())
        {
            output_origin = wf::origin(get_output()->get_layout_geometry());
        }

        if (!is_mapped())
        {
            /* If the view is not mapped yet, let it be configured as it
             * wishes. We will position it properly in ::map() */
            wlr_xwayland_surface_configure(xw, ev->x, ev->y, ev->width, ev->height);
            if ((ev->mask & XCB_CONFIG_WINDOW_X) && (ev->mask & XCB_CONFIG_WINDOW_Y))
            {
                this->self_positioned = true;
                toplevel->pending().geometry.x = ev->x - output_origin.x;
                toplevel->pending().geometry.y = ev->y - output_origin.y;
            }

            return;
        }

        /* Use old x/y values */
        ev->x = get_wm_geometry().x + output_origin.x;
        ev->y = get_wm_geometry().y + output_origin.y;
        configure_request(wlr_box{ev->x, ev->y, ev->width, ev->height});
    }

    wf::signal::connection_t<wf::xw::xwayland_toplevel_applied_state_signal> on_toplevel_applied =
        [&] (wf::xw::xwayland_toplevel_applied_state_signal *ev)
    {
        this->handle_toplevel_state_changed(ev->old_state);
    };

  public:
    wayfire_xwayland_view(wlr_xwayland_surface *xww) :
        wayfire_xwayland_view_base(xww)
    {}

    virtual void initialize() override
    {
        LOGE("new xwayland surface ", xw->title,
            " class: ", xw->class_t, " instance: ", xw->instance);

        this->toplevel = std::make_shared<wf::xw::xwayland_toplevel_t>(xw);
        toplevel->connect(&on_toplevel_applied);
        this->priv->toplevel = toplevel;
        wayfire_xwayland_view_base::initialize();

        on_map.set_callback([&] (void*)
        {
            check_create_main_surface(xw->surface, false);
            toplevel->set_main_surface(main_surface);
            toplevel->pending().mapped = true;
            wf::get_core().tx_manager->schedule_object(toplevel);
        });

        on_unmap.set_callback([&] (void*)
        {
            toplevel->set_main_surface(nullptr);
            toplevel->pending().mapped = false;
            wf::get_core().tx_manager->schedule_object(toplevel);
        });

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

        on_map.connect(&xw->events.map);
        on_unmap.connect(&xw->events.unmap);
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
        on_map.disconnect();
        on_unmap.disconnect();
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
                    get_output()->workarea->get_workarea());
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

        wayfire_xwayland_view_base::map(surface);
    }

    void commit() override
    {
        if (!xw->has_alpha)
        {
            pixman_region32_union_rect(
                &priv->wsurface->opaque_region, &priv->wsurface->opaque_region,
                0, 0, priv->wsurface->current.width, priv->wsurface->current.height);
        }

        wayfire_xwayland_view_base::commit();
        this->last_bounding_box = get_bounding_box();
    }

    void move(int x, int y) override
    {
        toplevel->pending().geometry.x = x;
        toplevel->pending().geometry.y = y;
        wf::get_core().tx_manager->schedule_object(toplevel);
    }

    void set_geometry(wf::geometry_t geometry) override
    {
        toplevel->pending().geometry = geometry;
        wf::get_core().tx_manager->schedule_object(toplevel);
    }

    void resize(int w, int h) override
    {
        toplevel->pending().geometry.width = w;
        toplevel->pending().geometry.height = h;
        wf::get_core().tx_manager->schedule_object(toplevel);
    }

    virtual void request_native_size() override
    {
        toplevel->request_native_size();
    }

    void set_tiled(uint32_t edges) override
    {
        wayfire_xwayland_view_base::set_tiled(edges);
        if (xw)
        {
            wlr_xwayland_surface_set_maximized(xw, !!edges);
        }
    }

    void set_fullscreen(bool full) override
    {
        wayfire_xwayland_view_base::set_fullscreen(full);
        if (xw)
        {
            wlr_xwayland_surface_set_fullscreen(xw, full);
        }
    }

    void set_minimized(bool minimized) override
    {
        wayfire_xwayland_view_base::set_minimized(minimized);
        if (xw)
        {
            wlr_xwayland_surface_set_minimized(xw, minimized);
        }
    }

    void set_output(wf::output_t *wo) override
    {
        output_geometry_changed.disconnect();
        wayfire_xwayland_view_base::set_output(wo);

        if (wo)
        {
            wo->connect(&output_geometry_changed);
            toplevel->set_output_offset(wf::origin(wo->get_layout_geometry()));
        } else
        {
            toplevel->set_output_offset({0, 0});
        }
    }

    void set_decoration(std::unique_ptr<wf::decorator_frame_t_t> frame) override
    {
        toplevel->set_decoration(frame.get());
        wayfire_xwayland_view_base::set_decoration(std::move(frame));
    }

    void handle_toplevel_state_changed(wf::toplevel_state_t old_state)
    {
        surface_root_node->set_offset(wf::origin(toplevel->calculate_base_geometry()));
        if (xw && !old_state.mapped && toplevel->current().mapped)
        {
            map(xw->surface);
        }

        if (old_state.mapped && !toplevel->current().mapped)
        {
            unmap();
        }

        view_damage_raw(self(), last_bounding_box);

        wf::view_geometry_changed_signal geometry_changed;
        geometry_changed.view = self();
        geometry_changed.old_geometry = old_state.geometry;
        emit(&geometry_changed);

        damage();
        last_bounding_box = this->get_surface_root_node()->get_bounding_box();
        wf::scene::update(this->get_surface_root_node(), wf::scene::update_flag::GEOMETRY);
    }

    wf::xw::view_type get_current_impl_type() const override
    {
        return wf::xw::view_type::NORMAL;
    }
};

#endif
