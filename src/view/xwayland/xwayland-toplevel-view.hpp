#pragma once

#include "config.h"
#include "view/view-impl.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/seat.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/toplevel.hpp"
#include "wayfire/util.hpp"
#include "xwayland-view-base.hpp"
#include <wayfire/workarea.hpp>
#include <wayfire/window-manager.hpp>
#include "xwayland-toplevel.hpp"
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/view-helpers.hpp>
#include "../toplevel-node.hpp"

#if WF_HAS_XWAYLAND

class wayfire_xwayland_view : public wf::toplevel_view_interface_t, public wayfire_xwayland_view_internal_base
{
    wf::wl_listener_wrapper on_request_move, on_request_resize,
        on_request_maximize, on_request_minimize, on_request_activate,
        on_request_fullscreen, on_set_hints, on_set_parent;
    wf::wl_listener_wrapper on_surface_commit;

    wf::wl_listener_wrapper on_set_decorations;
    std::shared_ptr<wf::xw::xwayland_toplevel_t> toplevel;

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

        LOGC(XWL, "Client configure ", self(), " at ", ev->x, ",", ev->y, " ", ev->width, "x", ev->height);
        if (!is_mapped())
        {
            /* If the view is not mapped yet, let it be configured as it
             * wishes. We will position it properly in ::map() */
            wlr_xwayland_surface_configure(xw, ev->x, ev->y, ev->width, ev->height);
            if ((ev->mask & XCB_CONFIG_WINDOW_X) && (ev->mask & XCB_CONFIG_WINDOW_Y))
            {
                int sx = ev->x - output_origin.x;
                int sy = ev->y - output_origin.y;
                this->set_property<int>("startup-x", sx);
                this->set_property<int>("startup-y", sy);
                toplevel->pending().geometry.x = sx;
                toplevel->pending().geometry.y = sy;
            }

            return;
        }

        configure_request(wlr_box{ev->x, ev->y, ev->width, ev->height});
    }

    void update_decorated()
    {
        uint32_t csd_flags = WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE |
            WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER;
        this->set_decoration_mode(xw->decorations & csd_flags);
    }

    void configure_request(wf::geometry_t configure_geometry)
    {
        configure_geometry = wf::expand_geometry_by_margins(configure_geometry, toplevel->pending().margins);
        wf::output_t *o = get_output();
        if (!o)
        {
            set_geometry(configure_geometry);
            return;
        }

        auto og = o->get_layout_geometry();

        // If client is fullscreen or tiled, do not allow it to reposition itself.
        // Otherwise, we will make sure it remains on its original workspace.
        if (pending_fullscreen() || pending_tiled_edges())
        {
            configure_geometry.x = get_pending_geometry().x + og.x;
            configure_geometry.y = get_pending_geometry().y + og.y;
            set_geometry(configure_geometry);
            return;
        }

        // Translate to output-local coordinates
        configure_geometry.x -= og.x;
        configure_geometry.y -= og.y;

        auto workarea = o->workarea->get_workarea();
        wayfire_toplevel_view main_view = wf::find_topmost_parent(wayfire_toplevel_view{this});

        // View workspace relative to current workspace
        if (main_view->get_wset())
        {
            auto view_ws = main_view->get_wset()->get_view_main_workspace(main_view);
            workarea.x += og.width * view_ws.x;
            workarea.y += og.height * view_ws.y;
        }

        // Ensure view remains visible
        if (!(configure_geometry & workarea))
        {
            configure_geometry = wf::clamp(configure_geometry, workarea);
        }

        set_geometry(configure_geometry);
    }

    wf::signal::connection_t<wf::xw::xwayland_toplevel_applied_state_signal> on_toplevel_applied =
        [&] (wf::xw::xwayland_toplevel_applied_state_signal *ev)
    {
        this->handle_toplevel_state_changed(ev->old_state);
    };

    std::shared_ptr<wf::toplevel_view_node_t> surface_root_node;

    // Reference count to this
    std::shared_ptr<wf::view_interface_t> _self_ref;

  public:
    wayfire_xwayland_view(wlr_xwayland_surface *xww) :
        wayfire_xwayland_view_internal_base(xww)
    {
        this->toplevel = std::make_shared<wf::xw::xwayland_toplevel_t>(xw);
        toplevel->connect(&on_toplevel_applied);
        this->priv->toplevel = toplevel;

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
            if (!this->activated && this->is_mapped())
            {
                wf::get_core().default_wm->focus_request({this}, true);
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
            auto parent = xw->parent ? (wf::view_interface_t*)(xw->parent->data) : nullptr;
            if (parent && !parent->is_mapped())
            {
                parent = nullptr;
            }

            if (wf::xw::has_type(xw, wf::xw::_NET_WM_WINDOW_TYPE_NORMAL))
            {
                parent = nullptr;
            }

            set_toplevel_parent(toplevel_cast(parent));
        });

        on_set_hints.set_callback([&] (void*)
        {
            wf::view_hints_changed_signal data;
            data.view = this;
            if (xw->hints && (xw->hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY))
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

        on_set_parent.connect(&xw->events.set_parent);
        on_set_hints.connect(&xw->events.set_hints);
        on_set_decorations.connect(&xw->events.set_decorations);

        on_request_move.connect(&xw->events.request_move);
        on_request_resize.connect(&xw->events.request_resize);
        on_request_activate.connect(&xw->events.request_activate);
        on_request_maximize.connect(&xw->events.request_maximize);
        on_request_minimize.connect(&xw->events.request_minimize);
        on_request_fullscreen.connect(&xw->events.request_fullscreen);
    }

    static std::shared_ptr<wayfire_xwayland_view> create(wlr_xwayland_surface *surface)
    {
        auto self = wf::view_interface_t::create<wayfire_xwayland_view>(surface);
        self->surface_root_node = std::make_shared<wf::toplevel_view_node_t>(self);
        self->set_surface_root_node(self->surface_root_node);

        // Set the output early, so that we can emit the signals on the output
        self->set_output(wf::get_core().seat->get_active_output());
        self->_initialize();

        self->update_decorated();
        // set initial parent
        self->on_set_parent.emit(nullptr);
        return self;
    }

    void set_activated(bool active) override
    {
        if (xw)
        {
            wlr_xwayland_surface_activate(xw, active);
        }

        wf::toplevel_view_interface_t::set_activated(active);
    }

    void handle_dissociate() override
    {
        on_surface_commit.disconnect();
    }

    virtual void destroy() override
    {
        on_set_parent.disconnect();
        on_set_hints.disconnect();
        on_set_decorations.disconnect();
        on_request_move.disconnect();
        on_request_resize.disconnect();
        on_request_activate.disconnect();
        on_request_maximize.disconnect();
        on_request_minimize.disconnect();
        on_request_fullscreen.disconnect();
        on_surface_commit.disconnect();

        wayfire_xwayland_view_internal_base::destroy();
    }

    void emit_view_map() override
    {
        /* Some X clients position themselves on map, and others let the window
         * manager determine this. We try to heuristically guess which of the
         * two cases we're dealing with by checking whether we have received
         * a valid ConfigureRequest before mapping */
        wf::view_implementation::emit_view_map_signal(self());
    }

    void store_xw_geometry_unmapped()
    {
        if ((xw->width > 0) && (xw->height > 0) && get_output())
        {
            /* Save geometry which the window has put itself in */
            wf::geometry_t save_geometry = {
                xw->x, xw->y, xw->width, xw->height
            };

            /* Make sure geometry is properly visible on the view output */
            save_geometry = save_geometry - wf::origin(get_output()->get_layout_geometry());
            save_geometry = wf::clamp(save_geometry, get_output()->workarea->get_workarea());
            wf::get_core().default_wm->update_last_windowed_geometry({this}, save_geometry);
        }
    }

    void handle_map_request(wlr_surface*) override
    {
        LOGC(VIEWS, "Start mapping ", self());
        this->main_surface = std::make_shared<wf::scene::wlr_surface_node_t>(xw->surface, false);
        priv->set_mapped_surface_contents(main_surface);
        toplevel->set_main_surface(main_surface);
        toplevel->pending().mapped = true;

        bool map_maximized = xw->maximized_horz || xw->maximized_vert;
        bool map_fs = xw->fullscreen;

        if (map_maximized || map_fs)
        {
            store_xw_geometry_unmapped();
        }

        wf::geometry_t desired_geometry = {
            xw->x,
            xw->y,
            xw->surface->current.width,
            xw->surface->current.height
        };

        if (get_output())
        {
            desired_geometry = desired_geometry + -wf::origin(get_output()->get_layout_geometry());
        }

        wf::adjust_view_pending_geometry_on_start_map(this, desired_geometry, map_fs, map_maximized);
        wf::get_core().tx_manager->schedule_object(toplevel);
    }

    void handle_unmap_request() override
    {
        LOGC(VIEWS, "Start unmapping ", self());
        emit_view_pre_unmap();
        // Store a reference to this until the view is actually unmapped with a transaction.
        _self_ref = shared_from_this();
        toplevel->set_main_surface(nullptr);
        toplevel->pending().mapped = false;
        wf::get_core().tx_manager->schedule_object(toplevel);
        on_surface_commit.disconnect();
    }

    void map(wlr_surface *surface)
    {
        LOGC(VIEWS, "Do map ", self());

        wf::adjust_view_output_on_map(this);
        do_map(surface, false);
        on_surface_commit.connect(&surface->events.commit);
        bool wants_focus = false;

        switch (wlr_xwayland_surface_icccm_input_model(xw))
        {
          /*
           * Abbreviated from ICCCM section 4.1.7 (Input Focus):
           *
           * Passive Input - The client expects keyboard input but never
           * explicitly sets the input focus.
           * Locally Active Input - The client expects keyboard input and
           * explicitly sets the input focus, but it only does so when one
           * of its windows already has the focus.
           *
           * Passive and Locally Active clients set the input field of
           * WM_HINTS to True, which indicates that they require window
           * manager assistance in acquiring the input focus.
           */
          case WLR_ICCCM_INPUT_MODEL_PASSIVE:
          case WLR_ICCCM_INPUT_MODEL_LOCAL:
            wants_focus = true;
            break;

          /*
           * Globally Active Input - The client expects keyboard input and
           * explicitly sets the input focus, even when it is in windows
           * the client does not own. ... It wants to prevent the window
           * manager from setting the input focus to any of its windows
           * [because it may or may not want focus].
           *
           * Globally Active client windows may receive a WM_TAKE_FOCUS
           * message from the window manager. If they want the focus, they
           * should respond with a SetInputFocus request.
           */
          case WLR_ICCCM_INPUT_MODEL_GLOBAL:
            /*
             * Assume that NORMAL and DIALOG windows are likely to
             * want focus. These window types should show up in the
             * Alt-Tab switcher and be automatically focused when
             * they become topmost.
             *
             * When we offer focus, we don't really focus the dialog.
             * Instead, by offering focus like that, we let the client
             * switch focus between its surfaces without involving the
             * compositor.
             */
            if (wlr_xwayland_surface_has_window_type(xw,
                WLR_XWAYLAND_NET_WM_WINDOW_TYPE_NORMAL) ||
                wlr_xwayland_surface_has_window_type(xw,
                    WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG))
            {
                wlr_xwayland_surface_offer_focus(xw);
            }

            break;

          /*
           * No Input - The client never expects keyboard input.
           *
           * No Input and Globally Active clients set the input field to
           * False, which requests that the window manager not set the
           * input focus to their top-level window.
           */
          case WLR_ICCCM_INPUT_MODEL_NONE:
            break;
        }

        if (wants_focus)
        {
            wf::get_core().default_wm->focus_request(self());
        }

        /* Might trigger repositioning */
        set_toplevel_parent(this->parent);
    }

    void unmap()
    {
        LOGC(VIEWS, "Do unmap ", self());
        do_unmap();
    }

    void commit()
    {
        if (!xw->has_alpha)
        {
            pixman_region32_union_rect(
                &priv->wsurface->opaque_region, &priv->wsurface->opaque_region,
                0, 0, priv->wsurface->current.width, priv->wsurface->current.height);
        }
    }

    virtual void request_native_size() override
    {
        toplevel->request_native_size();
    }

    void set_minimized(bool minimized) override
    {
        wf::toplevel_view_interface_t::set_minimized(minimized);
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

    void handle_toplevel_state_changed(wf::toplevel_state_t old_state)
    {
        surface_root_node->set_offset(wf::origin(toplevel->calculate_base_geometry()));
        if (xw && xw->surface && !old_state.mapped && toplevel->current().mapped)
        {
            map(xw->surface);
        }

        if (is_mapped() && !toplevel->current().mapped)
        {
            unmap();
        }

        wf::view_implementation::emit_toplevel_state_change_signals({this}, old_state);
        wf::scene::update(this->get_surface_root_node(), wf::scene::update_flag::GEOMETRY);

        if (!wf::get_core().tx_manager->is_object_pending(toplevel))
        {
            // Drop self-reference => object might be deleted afterwards
            _self_ref.reset();
        }
    }

    wf::xw::view_type get_current_impl_type() const override
    {
        return wf::xw::view_type::NORMAL;
    }

    bool has_client_decoration = true;
    void set_decoration_mode(bool use_csd)
    {
        bool was_decorated = should_be_decorated();
        this->has_client_decoration = use_csd;
        if (was_decorated != should_be_decorated())
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
               (xw && !wf::xw::has_type(xw, wf::xw::_NET_WM_WINDOW_TYPE_SPLASH));
    }
};

#endif
