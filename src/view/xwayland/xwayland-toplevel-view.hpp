#pragma once

#include "config.h"
#include "view/view-impl.hpp"
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/util.hpp"
#include "xwayland-view-base.hpp"
#include <wayfire/workarea.hpp>
#include <wayfire/window-manager.hpp>
#include "xwayland-toplevel.hpp"
#include <wayfire/txn/transaction-manager.hpp>

#if WF_HAS_XWAYLAND

class wayfire_xwayland_view : public wf::toplevel_view_interface_t, public wayfire_xwayland_view_base
{
    wf::wl_listener_wrapper on_request_move, on_request_resize,
        on_request_maximize, on_request_minimize, on_request_activate,
        on_request_fullscreen, on_set_parent, on_set_hints;

    wf::wl_listener_wrapper on_set_decorations;

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

    void update_decorated()
    {
        uint32_t csd_flags = WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE |
            WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER;
        this->set_decoration_mode(xw->decorations & csd_flags);
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
    wf::geometry_t translate_geometry_to_output(wf::output_t *output, wf::point_t ws_offset, wf::geometry_t g)
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

    void configure_request(wf::geometry_t configure_geometry)
    {
        /* Wayfire positions views relative to their output, but Xwayland
         * windows have a global positioning. So, we need to make sure that we
         * always transform between output-local coordinates and global
         * coordinates. Additionally, when clients send a configure request
         * after they have already been mapped, we keep the view on the
         * workspace where its center point was from last configure, in
         * case the current workspace is not where the view lives */
        wf::output_t *o = get_output();
        if (o)
        {
            auto view_workarea = (pending_fullscreen() ?
                o->get_relative_geometry() : o->workarea->get_workarea());
            auto og = o->get_layout_geometry();
            configure_geometry.x -= og.x;
            configure_geometry.y -= og.y;

            wayfire_toplevel_view view = {this};
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

    wf::signal::connection_t<wf::xw::xwayland_toplevel_applied_state_signal> on_toplevel_applied =
        [&] (wf::xw::xwayland_toplevel_applied_state_signal *ev)
    {
        this->handle_toplevel_state_changed(ev->old_state);
    };

    std::shared_ptr<wf::toplevel_view_node_t> surface_root_node;

  public:
    wayfire_xwayland_view(wlr_xwayland_surface *xww) :
        wayfire_xwayland_view_base(xww)
    {
        surface_root_node = std::make_shared<wf::toplevel_view_node_t>(this);
        this->set_surface_root_node(surface_root_node);
    }

    void set_activated(bool active) override
    {
        if (xw)
        {
            wlr_xwayland_surface_activate(xw, active);
        }
    }

    virtual void initialize() override
    {
        LOGE("new xwayland surface ", xw->title,
            " class: ", xw->class_t, " instance: ", xw->instance);

        this->toplevel = std::make_shared<wf::xw::xwayland_toplevel_t>(xw);
        toplevel->connect(&on_toplevel_applied);
        this->priv->toplevel = toplevel;

        _initialize();
        wf::view_interface_t::initialize();

        // Set the output early, so that we can emit the signals on the output
        if (!get_output())
        {
            set_output(wf::get_core().get_active_output());
        }

        on_map.set_callback([&] (void*)
        {
            this->main_surface = std::make_shared<wf::scene::wlr_surface_node_t>(xw->surface, false);
            priv->set_mapped_surface_contents(main_surface);
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

        on_request_move.set_callback([&] (void*)
        {
            wf::get_core().default_wm->move_request({this});
        });
        on_request_resize.set_callback([&] (auto data)
        {
            auto ev = static_cast<wlr_xwayland_resize_event*>(data);
            wf::get_core().default_wm->resize_request({this}, ev->edges);
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
            wf::get_core().default_wm->tile_request({this},
                (xw->maximized_horz && xw->maximized_vert) ? wf::TILED_EDGES_ALL : 0);
        });
        on_request_fullscreen.set_callback([&] (void*)
        {
            wf::get_core().default_wm->fullscreen_request({this}, get_output(), xw->fullscreen);
        });
        on_request_minimize.set_callback([&] (void *data)
        {
            auto ev = (wlr_xwayland_minimize_event*)data;
            wf::get_core().default_wm->minimize_request({this}, ev->minimize);
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

            set_toplevel_parent(toplevel_cast(parent));
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
        on_set_decorations.set_callback([&] (void*)
        {
            update_decorated();
        });
        update_decorated();

        on_map.connect(&xw->events.map);
        on_unmap.connect(&xw->events.unmap);
        on_set_parent.connect(&xw->events.set_parent);
        on_set_hints.connect(&xw->events.set_hints);
        on_set_decorations.connect(&xw->events.set_decorations);

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
        on_set_decorations.disconnect();
        on_request_move.disconnect();
        on_request_resize.disconnect();
        on_request_activate.disconnect();
        on_request_maximize.disconnect();
        on_request_minimize.disconnect();
        on_request_fullscreen.disconnect();

        wayfire_xwayland_view_base::destroy();

        /* Drop the internal reference */
        unref();
    }

    bool is_mapped() const override
    {
        return priv->wsurface != nullptr;
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
        priv->keyboard_focus_enabled = wlr_xwayland_or_surface_wants_focus(xw);
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

                toplevel->pending().geometry = save_geometry;
                wf::get_core().default_wm->update_last_windowed_geometry({this});
            }

            wf::get_core().default_wm->tile_request({this}, wf::TILED_EDGES_ALL);
        }

        if (xw->fullscreen)
        {
            wf::get_core().default_wm->fullscreen_request({this}, get_output(), true);
        }

        if (!this->pending_tiled_edges() && !xw->fullscreen)
        {
            configure_request({xw->x, xw->y, xw->width, xw->height});
        }

        priv->set_mapped(true);
        on_surface_commit.connect(&surface->events.commit);

        if (!parent)
        {
            wf::scene::readd_front(get_output()->wset()->get_node(), get_root_node());
            get_output()->wset()->add_view({this});
        }

        get_output()->focus_view(self(), true);

        damage();
        emit_view_map();
        /* Might trigger repositioning */
        set_toplevel_parent(this->parent);
    }

    void unmap() override
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

    void commit()
    {
        if (!xw->has_alpha)
        {
            pixman_region32_union_rect(
                &priv->wsurface->opaque_region, &priv->wsurface->opaque_region,
                0, 0, priv->wsurface->current.width, priv->wsurface->current.height);
        }

        this->last_bounding_box = get_bounding_box();
    }

    virtual void request_native_size() override
    {
        toplevel->request_native_size();
    }

    void set_minimized(bool minimized) override
    {
        if (xw)
        {
            wlr_xwayland_surface_set_minimized(xw, minimized);
        }
    }

    void set_output(wf::output_t *wo) override
    {
        output_geometry_changed.disconnect();
        wf::toplevel_view_interface_t::set_output(wo);

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
        toplevel_view_interface_t::set_decoration(std::move(frame));
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

        wf::scene::damage_node(get_root_node(), last_bounding_box);
        wf::emit_toplevel_state_change_signals({this}, old_state);

        damage();
        last_bounding_box = this->get_surface_root_node()->get_bounding_box();
        wf::scene::update(this->get_surface_root_node(), wf::scene::update_flag::GEOMETRY);
    }

    std::string get_app_id() override
    {
        return this->app_id;
    }

    std::string get_title() override
    {
        return this->title;
    }

    wf::xw::view_type get_current_impl_type() const override
    {
        return wf::xw::view_type::NORMAL;
    }

    wlr_surface *get_keyboard_focus_surface() override
    {
        return priv->keyboard_focus_enabled ? priv->wsurface : nullptr;
    }

    bool has_client_decoration = true;
    void set_decoration_mode(bool use_csd)
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

    bool should_be_decorated() override
    {
        return role == wf::VIEW_ROLE_TOPLEVEL && !has_client_decoration &&
               !has_type(wf::xw::_NET_WM_WINDOW_TYPE_SPLASH);
    }

    void ping() override
    {
        wayfire_xwayland_view_base::_ping();
    }

    void close() override
    {
        wayfire_xwayland_view_base::_close();
    }
};

#endif
