#include "wayfire/debug.hpp"
#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/render-manager.hpp>
#include "wayfire/core.hpp"
#include "wayfire/output.hpp"
#include "wayfire/view.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/decorator.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/signal-definitions.hpp"
#include "../core/core-impl.hpp"
#include "../core/seat/cursor.hpp"
#include "../core/seat/input-manager.hpp"
#include "view-impl.hpp"
#include <wayfire/scene-operations.hpp>

#include "xwayland/xwayland-helpers.hpp"
#include "xwayland/xwayland-view-base.hpp"

#if WF_HAS_XWAYLAND

xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_NORMAL;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_DIALOG;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_SPLASH;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_DND;

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

    wf::xw::view_type get_current_impl_type() const override
    {
        return wf::xw::view_type::UNMANAGED;
    }
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
    priv->keyboard_focus_enabled = (!xw->override_redirect ||
        wlr_xwayland_or_surface_wants_focus(xw));

    get_output()->workspace->add_view(self(), wf::LAYER_UNMANAGED);
    wf::wlr_view_t::map(surface);

    if (priv->keyboard_focus_enabled)
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

class wayfire_dnd_xwayland_view : public wayfire_unmanaged_xwayland_view
{
  protected:
    wf::wl_listener_wrapper on_set_geometry;

  public:
    using wayfire_unmanaged_xwayland_view::wayfire_unmanaged_xwayland_view;

    wf::xw::view_type get_current_impl_type() const override
    {
        return wf::xw::view_type::DND;
    }

    void destruct() override
    {
        LOGD("Destroying a Xwayland drag icon");
        wayfire_unmanaged_xwayland_view::destruct();
    }

    void deinitialize() override
    {
        wayfire_unmanaged_xwayland_view::deinitialize();
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

        wf::scene::add_front(wf::get_core().scene(), this->get_root_node());
    }

    void unmap() override
    {
        wf::scene::remove_child(this->get_root_node());
    }
};

void wayfire_xwayland_view_base::recreate_view()
{
    wf::xw::view_type target_type = wf::xw::view_type::NORMAL;
    if (this->is_dnd())
    {
        target_type = wf::xw::view_type::DND;
    } else if (this->is_unmanaged())
    {
        target_type = wf::xw::view_type::UNMANAGED;
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
      case wf::xw::view_type::DND:
        new_view = new wayfire_dnd_xwayland_view(xw_surf);
        break;

      case wf::xw::view_type::UNMANAGED:
        new_view = new wayfire_unmanaged_xwayland_view(xw_surf);
        break;

      case wf::xw::view_type::NORMAL:
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

    static wf::signal::connection_t<core_shutdown_signal> on_shutdown = [=] (core_shutdown_signal *ev)
    {
        wlr_xwayland_destroy(xwayland_handle);
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
        if (!wf::xw::load_basic_atoms(xwayland_handle->display_name))
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
        wf::get_core().connect(&on_shutdown);
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

    auto xc     = wf::get_core_impl().seat->priv->cursor->xcursor;
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
