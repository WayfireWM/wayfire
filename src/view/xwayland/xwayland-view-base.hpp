#pragma once

#include "config.h"
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

#include "../view-impl.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/view.hpp"
#include "xwayland-helpers.hpp"

#if WF_HAS_XWAYLAND

class wayfire_xwayland_view_base : public wf::view_interface_t
{
  protected:
    wf::wl_listener_wrapper on_destroy, on_unmap, on_map, on_configure,
        on_set_title, on_set_app_id, on_or_changed, on_set_decorations,
        on_ping_timeout, on_set_window_type;

    wf::wl_listener_wrapper on_surface_commit;
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;
    std::unique_ptr<wf::wlr_surface_controller_t> surface_controller;

    wlr_xwayland_surface *xw;
    /** The geometry requested by the client */
    bool self_positioned = false;

    std::string title, app_id;
    /** Used by view implementations when the app id changes */
    void handle_app_id_changed(std::string new_app_id)
    {
        this->app_id = new_app_id;
        wf::view_app_id_changed_signal data;
        data.view = self();
        emit(&data);
    }

    std::string get_app_id() override
    {
        return this->app_id;
    }

    /** Used by view implementations when the title changes */
    void handle_title_changed(std::string new_title)
    {
        this->title = new_title;
        wf::view_title_changed_signal data;
        data.view = self();
        emit(&data);
    }

    std::string get_title() override
    {
        return this->title;
    }

    /**
     * The bounding box of the view the last time it was rendered.
     *
     * This is used to damage the view when it is resized, because when a
     * transformer changes because the view is resized, we can't reliably
     * calculate the old view region to damage.
     */
    wf::geometry_t last_bounding_box{0, 0, 0, 0};

    /** The output geometry of the view */
    wf::geometry_t geometry{100, 100, 0, 0};

    wf::geometry_t get_output_geometry() override
    {
        return geometry;
    }

    wf::geometry_t get_wm_geometry() override
    {
        if (priv->frame)
        {
            return priv->frame->expand_wm_geometry(geometry);
        } else
        {
            return geometry;
        }
    }

    wlr_surface *get_keyboard_focus_surface() override
    {
        if (is_mapped() && priv->keyboard_focus_enabled)
        {
            return priv->wsurface;
        }

        return NULL;
    }

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

    bool has_client_decoration = true;
    void set_decoration_mode(bool use_csd)
    {
        bool was_decorated = should_be_decorated();
        this->has_client_decoration = use_csd;
        if ((was_decorated != should_be_decorated()) && is_mapped())
        {
            wf::view_decoration_state_updated_signal data;
            data.view = self();

            this->emit(&data);
            if (get_output())
            {
                get_output()->emit(&data);
            }
        }
    }

  public:
    wayfire_xwayland_view_base(wlr_xwayland_surface *xww) :
        view_interface_t(), xw(xww)
    {
        on_surface_commit.set_callback([&] (void*) { commit(); });
    }

    void adjust_anchored_edge(wf::dimensions_t new_size)
    {
        if (priv->edges)
        {
            auto wm = get_wm_geometry();
            if (priv->edges & WLR_EDGE_LEFT)
            {
                wm.x += geometry.width - new_size.width;
            }

            if (priv->edges & WLR_EDGE_TOP)
            {
                wm.y += geometry.height - new_size.height;
            }

            set_position(wm.x, wm.y, get_wm_geometry(), false);
        }
    }

    void set_position(int x, int y, wf::geometry_t old_geometry, bool send_signal)
    {
        auto obox = get_output_geometry();
        auto wm   = get_wm_geometry();

        wf::view_geometry_changed_signal data;
        data.view = self();
        data.old_geometry = old_geometry;

        view_damage_raw(self(), last_bounding_box);
        /* obox.x - wm.x is the current difference in the output and wm geometry */
        geometry.x = x + obox.x - wm.x;
        geometry.y = y + obox.y - wm.y;

        /* Make sure that if we move the view while it is unmapped, its snapshot
         * is still valid coordinates */
        priv->offscreen_buffer = priv->offscreen_buffer.translated({
            x - data.old_geometry.x, y - data.old_geometry.y,
        });

        damage();

        if (send_signal)
        {
            emit(&data);
            wf::get_core().emit(&data);
            if (get_output())
            {
                get_output()->emit(&data);
            }
        }

        last_bounding_box = get_bounding_box();
        wf::scene::update(this->get_surface_root_node(), wf::scene::update_flag::GEOMETRY);
    }

    void update_size()
    {
        if (!is_mapped())
        {
            return;
        }

        wf::dimensions_t current_size{
            priv->wsurface->current.width,
            priv->wsurface->current.height,
        };

        if ((current_size.width == geometry.width) &&
            (current_size.height == geometry.height))
        {
            return;
        }

        /* Damage current size */
        view_damage_raw(self(), last_bounding_box);
        adjust_anchored_edge(current_size);

        wf::view_geometry_changed_signal data;
        data.view = self();
        data.old_geometry = get_wm_geometry();

        geometry.width  = current_size.width;
        geometry.height = current_size.height;

        /* Damage new size */
        last_bounding_box = get_bounding_box();
        view_damage_raw(self(), last_bounding_box);
        emit(&data);
        wf::get_core().emit(&data);
        if (get_output())
        {
            get_output()->emit(&data);
        }

        wf::scene::update(this->get_surface_root_node(), wf::scene::update_flag::GEOMETRY);
    }

    virtual void commit()
    {
        wf::region_t dmg;
        wlr_surface_get_effective_damage(priv->wsurface, dmg.to_pixman());
        wf::scene::damage_node(this->get_surface_root_node(), dmg);

        update_size();

        /* Clear the resize edges.
         * This is must be done here because if the user(or plugin) resizes too fast,
         * the shell client might still haven't configured the surface, and in this
         * case the next commit(here) needs to still have access to the gravity */
        if (!priv->in_continuous_resize)
        {
            priv->edges = 0;
        }

        this->last_bounding_box = get_bounding_box();
    }

    virtual void map(wlr_surface *surface)
    {
        wf::scene::set_node_enabled(priv->root_node, true);
        priv->wsurface = surface;

        this->main_surface = std::make_shared<wf::scene::wlr_surface_node_t>(surface, true);
        priv->surface_root_node->set_children_list({main_surface});
        wf::scene::update(priv->surface_root_node, wf::scene::update_flag::CHILDREN_LIST);
        surface_controller = std::make_unique<wf::wlr_surface_controller_t>(surface, priv->surface_root_node);
        on_surface_commit.connect(&surface->events.commit);

        update_size();

        if (role == wf::VIEW_ROLE_TOPLEVEL)
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
    }

    virtual void unmap()
    {
        damage();
        emit_view_pre_unmap();
        set_decoration(nullptr);

        main_surface   = nullptr;
        priv->wsurface = nullptr;
        on_surface_commit.disconnect();
        surface_controller = nullptr;
        priv->surface_root_node->set_children_list({});
        wf::scene::update(priv->surface_root_node, wf::scene::update_flag::CHILDREN_LIST);

        emit_view_unmap();
        wf::scene::set_node_enabled(priv->root_node, false);
    }

    virtual void initialize() override
    {
        wf::view_interface_t::initialize();

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

    virtual void destroy()
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

        priv->is_alive = false;
        /* Drop the internal reference */
        unref();
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
        return role == wf::VIEW_ROLE_TOPLEVEL && !has_client_decoration &&
               !has_type(wf::xw::_NET_WM_WINDOW_TYPE_SPLASH);
    }

    bool should_resize_client(wf::dimensions_t request, wf::dimensions_t current_geometry)
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
            return request != current_geometry;
        } else
        {
            return request != last_size_request;
        }
    }

    bool is_mapped() const override
    {
        return priv->wsurface != nullptr;
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

        wf::view_interface_t::close();
    }

    void set_activated(bool active) override
    {
        if (xw)
        {
            wlr_xwayland_surface_activate(xw, active);
        }

        wf::view_interface_t::set_activated(active);
    }

    void move(int x, int y) override
    {
        set_position(x, y, get_wm_geometry(), true);
        if (!priv->in_continuous_move)
        {
            send_configure();
        }
    }

    void set_geometry(wf::geometry_t geometry) override
    {
        move(geometry.x, geometry.y);
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

    wf::dimensions_t last_size_request = {0, 0};
    void send_configure()
    {
        send_configure(last_size_request.width, last_size_request.height);
    }

    virtual void set_output(wf::output_t *wo) override
    {
        output_geometry_changed.disconnect();
        wf::view_interface_t::set_output(wo);

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
