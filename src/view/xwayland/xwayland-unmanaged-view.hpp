#pragma once

#include "config.h"
#include "wayfire/util.hpp"
#include "xwayland-view-base.hpp"
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-operations.hpp>

#if WF_HAS_XWAYLAND

class wayfire_unmanaged_xwayland_view : public wayfire_xwayland_view_base
{
  protected:
    wf::wl_listener_wrapper on_set_geometry;
    wf::wl_listener_wrapper on_map;
    wf::wl_listener_wrapper on_unmap;

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

    void set_position(int x, int y, wf::geometry_t old_geometry, bool send_signal)
    {
        wf::view_geometry_changed_signal data;
        data.view = self();
        data.old_geometry = old_geometry;

        view_damage_raw(self(), last_bounding_box);
        geometry.x = x;
        geometry.y = y;

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

    void handle_client_configure(wlr_xwayland_surface_configure_event *ev) override
    {
        wf::point_t output_origin = {0, 0};
        if (get_output())
        {
            output_origin = wf::origin(get_output()->get_layout_geometry());
        }

        if ((ev->mask & XCB_CONFIG_WINDOW_X) && (ev->mask & XCB_CONFIG_WINDOW_Y))
        {
            this->self_positioned = true;
        } else
        {
            /* Use old x/y values */
            ev->x = geometry.x + output_origin.x;
            ev->y = geometry.y + output_origin.y;
        }

        if (!is_mapped())
        {
            /* If the view is not mapped yet, let it be configured as it
             * wishes. We will position it properly in ::map() */
            wlr_xwayland_surface_configure(xw, ev->x, ev->y, ev->width, ev->height);
            if ((ev->mask & XCB_CONFIG_WINDOW_X) && (ev->mask & XCB_CONFIG_WINDOW_Y))
            {
                this->geometry.x = ev->x - output_origin.x;
                this->geometry.y = ev->y - output_origin.y;
            }
        } else
        {
            configure_request({ev->x, ev->y, ev->width, ev->height});
        }
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

  public:
    wayfire_unmanaged_xwayland_view(wlr_xwayland_surface *xww) : wayfire_xwayland_view_base(xww)
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

                move(geometry.x, geometry.y);
            }
        });
        on_map.set_callback([&] (void*) { map(xw->surface); });
        on_unmap.set_callback([&] (void*) { unmap(); });

        on_map.connect(&xw->events.map);
        on_unmap.connect(&xw->events.unmap);
        on_set_geometry.connect(&xw->events.set_geometry);
    }

    int global_x, global_y;
    void map(wlr_surface *surface) override
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
        move(xw->x - real_output_geometry.x, xw->y - real_output_geometry.y);

        if (wo != get_output())
        {
            set_output(wo);
        }

        damage();

        /* We update the keyboard focus before emitting the map event, so that
         * plugins can detect that this view can have keyboard focus.
         *
         * Note: only actual override-redirect views should get their focus disabled */
        priv->keyboard_focus_enabled = (!xw->override_redirect ||
            wlr_xwayland_or_surface_wants_focus(xw));

        wf::scene::readd_front(get_output()->node_for_layer(wf::scene::layer::UNMANAGED), get_root_node());
        wayfire_xwayland_view_base::map(surface);

        if (priv->keyboard_focus_enabled)
        {
            get_output()->focus_view(self(), true);
        }
    }

    void commit() override
    {
        wayfire_xwayland_view_base::commit();
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

    void destroy() override
    {
        on_map.disconnect();
        on_unmap.disconnect();
        on_set_geometry.disconnect();
        wayfire_xwayland_view_base::destroy();
    }

    bool should_be_decorated() override
    {
        return (!xw->override_redirect && !this->has_client_decoration);
    }

    wf::dimensions_t last_size_request = {0, 0};

    void move(int x, int y) override
    {
        set_position(x, y, get_wm_geometry(), true);
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

    void set_geometry(wf::geometry_t geometry) override
    {
        move(geometry.x, geometry.y);
        resize(geometry.width, geometry.height);
    }

    wf::xw::view_type get_current_impl_type() const override
    {
        return wf::xw::view_type::UNMANAGED;
    }
};

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

        wf::scene::readd_front(wf::get_core().scene(), this->get_root_node());
    }

    void unmap() override
    {
        wf::scene::remove_child(this->get_root_node());
    }
};

#endif
