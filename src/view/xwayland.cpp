#include "wayfire/debug.hpp"
#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/render-manager.hpp>
#include "wayfire/core.hpp"
#include "wayfire/output.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/decorator.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/signal-definitions.hpp"
#include "../core/core-impl.hpp"
#include "../core/seat/cursor.hpp"
#include "../core/seat/input-manager.hpp"
#include "view-impl.hpp"

#if WF_HAS_XWAYLAND

enum class xwayland_view_type_t
{
    NORMAL,
    UNMANAGED,
    DND,
};

class wayfire_xwayland_view_base : public wf::wlr_view_t
{
  protected:
    static xcb_atom_t _NET_WM_WINDOW_TYPE_NORMAL;
    static xcb_atom_t _NET_WM_WINDOW_TYPE_DIALOG;
    static xcb_atom_t _NET_WM_WINDOW_TYPE_SPLASH;
    static xcb_atom_t _NET_WM_WINDOW_TYPE_DND;

    static void load_atom(xcb_connection_t *connection,
        xcb_atom_t& atom, const std::string& name)
    {
        auto cookie = xcb_intern_atom(connection, 0, name.length(), name.c_str());

        xcb_generic_error_t *error = NULL;
        xcb_intern_atom_reply_t *reply;
        reply = xcb_intern_atom_reply(connection, cookie, &error);

        bool success = !error && reply;
        if (success)
        {
            atom = reply->atom;
        }

        free(reply);
        free(error);
    }

  public:
    static bool load_atoms(const char *server_name)
    {
        auto connection = xcb_connect(server_name, NULL);
        if (!connection || xcb_connection_has_error(connection))
        {
            return false;
        }

        load_atom(connection, _NET_WM_WINDOW_TYPE_NORMAL,
            "_NET_WM_WINDOW_TYPE_NORMAL");
        load_atom(connection, _NET_WM_WINDOW_TYPE_DIALOG,
            "_NET_WM_WINDOW_TYPE_DIALOG");
        load_atom(connection, _NET_WM_WINDOW_TYPE_SPLASH,
            "_NET_WM_WINDOW_TYPE_SPLASH");
        load_atom(connection, _NET_WM_WINDOW_TYPE_DND,
            "_NET_WM_WINDOW_TYPE_DND");

        xcb_disconnect(connection);
        return true;
    }

  protected:
    wf::wl_listener_wrapper on_destroy, on_unmap, on_map, on_configure,
        on_set_title, on_set_app_id, on_or_changed, on_set_decorations,
        on_ping_timeout, on_set_window_type;

    wlr_xwayland_surface *xw;
    /** The geometry requested by the client */
    bool self_positioned = false;

    wf::signal_connection_t output_geometry_changed{[this] (wf::signal_data_t*)
        {
            if (is_mapped())
            {
                auto wm_geometry = get_wm_geometry();
                move(wm_geometry.x, wm_geometry.y);
            }
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
        if (has_type(_NET_WM_WINDOW_TYPE_DIALOG) ||
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
            !this->has_type(_NET_WM_WINDOW_TYPE_NORMAL))
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
        return this->has_type(_NET_WM_WINDOW_TYPE_DND);
    }

    /**
     * Get the current implementation type.
     */
    virtual xwayland_view_type_t get_current_impl_type() const = 0;

  public:
    wayfire_xwayland_view_base(wlr_xwayland_surface *xww) :
        wlr_view_t(), xw(xww)
    {}

    virtual void initialize() override
    {
        wf::wlr_view_t::initialize();
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
                    xw->window_type[0] != _NET_WM_WINDOW_TYPE_NORMAL);

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
            !has_type(_NET_WM_WINDOW_TYPE_SPLASH));
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

        if (view_impl->frame)
        {
            configure_geometry =
                view_impl->frame->expand_wm_geometry(configure_geometry);
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
        if (!view_impl->in_continuous_move)
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
            wo->connect_signal("output-configuration-changed",
                &output_geometry_changed);
        }

        /* Update the real position */
        if (is_mapped())
        {
            send_configure();
        }
    }
};

xcb_atom_t wayfire_xwayland_view_base::_NET_WM_WINDOW_TYPE_NORMAL;
xcb_atom_t wayfire_xwayland_view_base::_NET_WM_WINDOW_TYPE_DIALOG;
xcb_atom_t wayfire_xwayland_view_base::_NET_WM_WINDOW_TYPE_SPLASH;
xcb_atom_t wayfire_xwayland_view_base::_NET_WM_WINDOW_TYPE_DND;

class wayfire_unmanaged_xwayland_view : public wayfire_xwayland_view_base
{
  protected:
    wf::wl_listener_wrapper on_set_geometry;

  public:
    wayfire_unmanaged_xwayland_view(wlr_xwayland_surface *xww);

    int global_x, global_y;

    void map(wlr_surface *surface) override;
    void destroy() override;

    bool should_be_decorated() override;

    xwayland_view_type_t get_current_impl_type() const override
    {
        return xwayland_view_type_t::UNMANAGED;
    }

    ~wayfire_unmanaged_xwayland_view()
    {}
};

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
                emit_signal("view-focus-request", &data);
                wf::get_core().emit_signal("view-focus-request", &data);
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

            auto parent = xw->parent ?
                wf::wf_view_from_void(xw->parent->data)->self() : nullptr;

            // Make sure the parent is mapped, and that we are not a toplevel view
            if (parent)
            {
                if (!parent->is_mapped() ||
                    this->has_type(_NET_WM_WINDOW_TYPE_NORMAL))
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

            wf::get_core().emit_signal("view-hints-changed", &data);
            this->emit_signal("hints-changed", &data);
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
        view_impl->keyboard_focus_enabled =
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
                view_impl->update_windowed_geometry(self(), save_geometry);
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
        create_toplevel();
    }

    void commit() override
    {
        if (!xw->has_alpha)
        {
            pixman_region32_union_rect(
                &surface->opaque_region, &surface->opaque_region,
                0, 0, surface->current.width, surface->current.height);
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
        if (!view_impl->in_continuous_move)
        {
            send_configure();
        }
    }

    void resize(int w, int h) override
    {
        if (view_impl->frame)
        {
            view_impl->frame->calculate_resize_size(w, h);
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

    virtual void toplevel_send_app_id() override
    {
        if (!toplevel_handle)
        {
            return;
        }

        /* Xwayland windows have two "app-id"s - the class and the instance.
         * Some apps' icons can be found by looking up the class, for others
         * the instance. So, just like the workaround for gtk-shell, we can
         * send both the instance and the class to clients, so that they can
         * find the appropriate icons. */
        std::string app_id;
        auto default_app_id  = get_app_id();
        auto instance_app_id = nonull(xw->instance);

        std::string app_id_mode =
            wf::option_wrapper_t<std::string>("workarounds/app_id_mode");
        if (app_id_mode == "full")
        {
            app_id = default_app_id + " " + instance_app_id;
        } else
        {
            app_id = default_app_id;
        }

        wlr_foreign_toplevel_handle_v1_set_app_id(
            toplevel_handle, app_id.c_str());
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

    xwayland_view_type_t get_current_impl_type() const override
    {
        return xwayland_view_type_t::NORMAL;
    }
};

wayfire_unmanaged_xwayland_view::wayfire_unmanaged_xwayland_view(
    wlr_xwayland_surface *xww) :
    wayfire_xwayland_view_base(xww)
{
    LOGE("new unmanaged xwayland surface ", xw->title, " class: ", xw->class_t,
        " instance: ", xw->instance);

    xw->data = this;
    role     = wf::VIEW_ROLE_UNMANAGED;

    on_set_geometry.set_callback([&] (void*)
    {
        /* Xwayland O-R views manage their position on their own. So we need to
         * update their position on each commit, if the position changed. */
        if ((global_x != xw->x) || (global_y != xw->y))
        {
            geometry.x = global_x = xw->x;
            geometry.y = global_y = xw->y;

            if (get_output())
            {
                auto real_output = get_output()->get_layout_geometry();
                geometry.x -= real_output.x;
                geometry.y -= real_output.y;
            }

            wf::wlr_view_t::move(geometry.x, geometry.y);
        }
    });

    on_set_geometry.connect(&xw->events.set_geometry);
}

void wayfire_unmanaged_xwayland_view::map(wlr_surface *surface)
{
    /* move to the output where our center is
     * FIXME: this is a bad idea, because a dropdown menu might get sent to
     * an incorrect output. However, no matter how we calculate the real
     * output, we just can't be 100% compatible because in X all windows are
     * positioned in a global coordinate space */
    auto wo = wf::get_core().output_layout->get_output_at(
        xw->x + surface->current.width / 2, xw->y + surface->current.height / 2);

    if (!wo)
    {
        /* if surface center is outside of anything, try to check the output
         * where the pointer is */
        auto gc = wf::get_core().get_cursor_position();
        wo = wf::get_core().output_layout->get_output_at(gc.x, gc.y);
    }

    if (!wo)
    {
        wo = wf::get_core().get_active_output();
    }

    assert(wo);

    auto real_output_geometry = wo->get_layout_geometry();

    global_x = xw->x;
    global_y = xw->y;
    wf::wlr_view_t::move(xw->x - real_output_geometry.x,
        xw->y - real_output_geometry.y);

    if (wo != get_output())
    {
        if (get_output())
        {
            get_output()->workspace->remove_view(self());
        }

        set_output(wo);
    }

    damage();

    /* We update the keyboard focus before emitting the map event, so that
     * plugins can detect that this view can have keyboard focus.
     *
     * Note: only actual override-redirect views should get their focus disabled */
    view_impl->keyboard_focus_enabled = (!xw->override_redirect ||
        wlr_xwayland_or_surface_wants_focus(xw));

    get_output()->workspace->add_view(self(), wf::LAYER_UNMANAGED);
    wf::wlr_view_t::map(surface);

    if (view_impl->keyboard_focus_enabled)
    {
        get_output()->focus_view(self(), true);
    }
}

bool wayfire_unmanaged_xwayland_view::should_be_decorated()
{
    return (!xw->override_redirect && !this->has_client_decoration);
}

void wayfire_unmanaged_xwayland_view::destroy()
{
    on_set_geometry.disconnect();
    wayfire_xwayland_view_base::destroy();
}

// Xwayland DnD view
static wayfire_view dnd_view;

class wayfire_dnd_xwayland_view : public wayfire_unmanaged_xwayland_view
{
  protected:
    wf::wl_listener_wrapper on_set_geometry;

  public:
    using wayfire_unmanaged_xwayland_view::wayfire_unmanaged_xwayland_view;

    xwayland_view_type_t get_current_impl_type() const override
    {
        return xwayland_view_type_t::DND;
    }

    void simple_render(const wf::framebuffer_t& fb,
        int x, int y, const wf::region_t& damage) override
    {
        wayfire_unmanaged_xwayland_view::simple_render(fb, x, y, damage);

        timespec repaint_ended;
        clockid_t presentation_clock =
            wlr_backend_get_presentation_clock(wf::get_core_impl().backend);
        clock_gettime(presentation_clock, &repaint_ended);
        send_frame_done(repaint_ended);
    }

    void destruct() override
    {
        LOGD("Destroying a Xwayland drag icon");
        if (dnd_view.get() == this)
        {
            dnd_view = nullptr;
        }

        wayfire_unmanaged_xwayland_view::destruct();
    }

    void deinitialize() override
    {
        wayfire_unmanaged_xwayland_view::deinitialize();
    }

    void damage_surface_box(const wlr_box&) override
    {
        damage();
    }

    wf::geometry_t last_global_bbox = {0, 0, 0, 0};

    void damage() override
    {
        if (!get_output())
        {
            return;
        }

        auto bbox = get_bounding_box() +
            wf::origin(this->get_output()->get_layout_geometry());

        for (auto& output : wf::get_core().output_layout->get_outputs())
        {
            auto local_bbox = bbox + -wf::origin(output->get_layout_geometry());
            output->render->damage(local_bbox);
            local_bbox = last_global_bbox +
                -wf::origin(output->get_layout_geometry());
            output->render->damage(local_bbox);
        }

        last_global_bbox = bbox;
    }

    void map(wlr_surface *surface) override
    {
        LOGD("Mapping a Xwayland drag icon");
        this->set_output(wf::get_core().get_active_output());
        wayfire_xwayland_view_base::map(surface);
        this->damage();
    }
};

void wayfire_xwayland_view_base::recreate_view()
{
    xwayland_view_type_t target_type = xwayland_view_type_t::NORMAL;
    if (this->is_dnd())
    {
        target_type = xwayland_view_type_t::DND;
    } else if (this->is_unmanaged())
    {
        target_type = xwayland_view_type_t::UNMANAGED;
    }

    if (target_type == this->get_current_impl_type())
    {
        // Nothing changed
        return;
    }

    /*
     * Copy xw and mapped status into the stack, because "this" may be destroyed
     * at some point of this function.
     */
    auto xw_surf    = this->xw;
    bool was_mapped = is_mapped();

    // destroy the view (unmap + destroy)
    if (was_mapped)
    {
        unmap();
    }

    destroy();

    // Create the new view.
    // Take care! The new_view pointer is passed to core as unique_ptr
    wayfire_xwayland_view_base *new_view;
    switch (target_type)
    {
      case xwayland_view_type_t::DND:
        new_view = new wayfire_dnd_xwayland_view(xw_surf);
        ::dnd_view = new_view;
        break;

      case xwayland_view_type_t::UNMANAGED:
        new_view = new wayfire_unmanaged_xwayland_view(xw_surf);
        wf::get_core().add_view(std::unique_ptr<view_interface_t>(new_view));
        break;

      case xwayland_view_type_t::NORMAL:
        new_view = new wayfire_xwayland_view(xw_surf);
        break;
    }

    wf::get_core().add_view(std::unique_ptr<view_interface_t>(new_view));
    if (was_mapped)
    {
        new_view->map(xw_surf->surface);
    }
}

static wlr_xwayland *xwayland_handle = nullptr;
#endif

void wf::init_xwayland()
{
#if WF_HAS_XWAYLAND
    static wf::wl_listener_wrapper on_created;
    static wf::wl_listener_wrapper on_ready;

    static signal_connection_t on_shutdown{[&] (void*)
        {
            wlr_xwayland_destroy(xwayland_handle);
        }
    };

    on_created.set_callback([] (void *data)
    {
        auto xsurf = (wlr_xwayland_surface*)data;
        if (xsurf->override_redirect)
        {
            wf::get_core().add_view(
                std::make_unique<wayfire_unmanaged_xwayland_view>(xsurf));
        } else
        {
            wf::get_core().add_view(
                std::make_unique<wayfire_xwayland_view>(xsurf));
        }
    });

    on_ready.set_callback([&] (void *data)
    {
        if (!wayfire_xwayland_view_base::load_atoms(xwayland_handle->display_name))
        {
            LOGE("Failed to load Xwayland atoms.");
        } else
        {
            LOGD("Successfully loaded Xwayland atoms.");
        }

        wlr_xwayland_set_seat(xwayland_handle,
            wf::get_core().get_current_seat());
        xwayland_update_default_cursor();
    });

    xwayland_handle = wlr_xwayland_create(wf::get_core().display,
        wf::get_core_impl().compositor, false);

    if (xwayland_handle)
    {
        on_created.connect(&xwayland_handle->events.new_surface);
        on_ready.connect(&xwayland_handle->events.ready);
        wf::get_core().connect_signal("shutdown", &on_shutdown);
    }

#endif
}

void wf::xwayland_update_default_cursor()
{
#if WF_HAS_XWAYLAND
    if (!xwayland_handle)
    {
        return;
    }

    auto xc     = wf::get_core_impl().seat->cursor->xcursor;
    auto cursor = wlr_xcursor_manager_get_xcursor(xc, "left_ptr", 1);
    if (cursor && (cursor->image_count > 0))
    {
        auto image = cursor->images[0];
        wlr_xwayland_set_cursor(xwayland_handle, image->buffer,
            image->width * 4, image->width, image->height,
            image->hotspot_x, image->hotspot_y);
    }

#endif
}

void wf::xwayland_bring_to_front(wlr_surface *surface)
{
#if WF_HAS_XWAYLAND
    if (wlr_surface_is_xwayland_surface(surface))
    {
        auto xw = wlr_xwayland_surface_from_wlr_surface(surface);
        wlr_xwayland_surface_restack(xw, NULL, XCB_STACK_MODE_ABOVE);
    }

#endif
}

std::string wf::xwayland_get_display()
{
#if WF_HAS_XWAYLAND

    return xwayland_handle ? nonull(xwayland_handle->display_name) : "";
#else

    return "";
#endif
}

wayfire_view wf::get_xwayland_drag_icon()
{
#if WF_HAS_XWAYLAND
    if (dnd_view && dnd_view->is_mapped() && dnd_view->get_output())
    {
        return dnd_view.get();
    }

#endif

    return nullptr;
}
