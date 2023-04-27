#pragma once

#include "config.h"
#include "xwayland-view-base.hpp"
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-operations.hpp>

#if WF_HAS_XWAYLAND

class wayfire_unmanaged_xwayland_view : public wayfire_xwayland_view_base
{
  protected:
    wf::wl_listener_wrapper on_set_geometry;

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

                wf::wlr_view_t::move(geometry.x, geometry.y);
            }
        });

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

    void destroy() override
    {
        on_set_geometry.disconnect();
        wayfire_xwayland_view_base::destroy();
    }

    bool should_be_decorated() override
    {
        return (!xw->override_redirect && !this->has_client_decoration);
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

        wf::scene::add_front(wf::get_core().scene(), this->get_root_node());
    }

    void unmap() override
    {
        wf::scene::remove_child(this->get_root_node());
    }
};

#endif
