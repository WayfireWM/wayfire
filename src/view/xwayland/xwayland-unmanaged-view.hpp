#pragma once

#include "config.h"
#include "wayfire/output.hpp"
#include "wayfire/unstable/translation-node.hpp"
#include "wayfire/util.hpp"
#include "wayfire/view.hpp"
#include "xwayland-view-base.hpp"
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-operations.hpp>
#include "../core/core-impl.hpp"
#include "../core/seat/seat-impl.hpp"
#include "../view-keyboard-interaction.hpp"

#if WF_HAS_XWAYLAND

namespace wf
{
class xwayland_unmanaged_view_node_t : public wf::scene::translation_node_t, public view_node_tag_t
{
  public:
    xwayland_unmanaged_view_node_t(wayfire_view view) : view_node_tag_t(view)
    {
        this->kb_interaction = std::make_unique<view_keyboard_interaction_t>(view);
        on_view_destroy = [=] (view_destruct_signal *ev)
        {
            this->view = nullptr;
            this->kb_interaction = std::make_unique<keyboard_interaction_t>();
        };

        view->connect(&on_view_destroy);
    }

    wf::keyboard_focus_node_t keyboard_refocus(wf::output_t *output) override
    {
        if (!view || !view->get_keyboard_focus_surface())
        {
            return wf::keyboard_focus_node_t{};
        }

        if (output != view->get_output())
        {
            return wf::keyboard_focus_node_t{};
        }

        const uint64_t output_last_ts = view->get_output()->get_last_focus_timestamp();
        const uint64_t our_ts = keyboard_interaction().last_focus_timestamp;
        auto cur_focus = wf::get_core_impl().seat->priv->keyboard_focus.get();
        bool has_focus = (cur_focus == this) || (our_ts == output_last_ts);
        if (has_focus)
        {
            return wf::keyboard_focus_node_t{this, focus_importance::REGULAR};
        }

        return wf::keyboard_focus_node_t{};
    }

    keyboard_interaction_t& keyboard_interaction() override
    {
        return *kb_interaction;
    }

    std::string stringify() const override
    {
        std::ostringstream out;
        out << this->view;
        return "unmanaged " + out.str() + " " + stringify_flags();
    }

  protected:
    wayfire_toplevel_view view;
    std::unique_ptr<keyboard_interaction_t> kb_interaction;
    wf::signal::connection_t<view_destruct_signal> on_view_destroy;
};
}

class wayfire_unmanaged_xwayland_view : public wf::view_interface_t, public wayfire_xwayland_view_base
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

    // The geometry of the view in output-layout coordinates (same as Xwayland).
    wf::geometry_t geometry{100, 100, 0, 0};


    void handle_client_configure(wlr_xwayland_surface_configure_event *ev) override
    {
        // We accept the client requests without any modification when it comes to unmanaged views.
        wlr_xwayland_surface_configure(xw, ev->x, ev->y, ev->width, ev->height);
        update_geometry_from_xsurface();
    }

    void update_geometry_from_xsurface()
    {
        wf::scene::damage_node(get_root_node(), last_bounding_box);

        geometry.x     = xw->x;
        geometry.y     = xw->y;
        geometry.width = xw->width;
        geometry.height = xw->height;

        wf::point_t new_position = wf::origin(geometry);

        // Move to the correct output, if the xsurface has changed geometry
        wf::pointf_t midpoint = {geometry.x + geometry.width / 2.0, geometry.y + geometry.height / 2.0};
        wf::output_t *wo = wf::get_core().output_layout->get_output_coords_at(midpoint, midpoint);
        if (wo != get_output())
        {
            set_output(wo);
            if (wo && (get_current_impl_type() != wf::xw::view_type::DND))
            {
                new_position = new_position - wf::origin(wo->get_layout_geometry());
                wf::scene::readd_front(wo->node_for_layer(wf::scene::layer::UNMANAGED), get_root_node());
            }
        }

        surface_root_node->set_offset(new_position);
        last_bounding_box = get_bounding_box();
        wf::scene::damage_node(get_root_node(), last_bounding_box);
        wf::scene::update(surface_root_node, wf::scene::update_flag::GEOMETRY);
    }

    bool is_mapped() const override
    {
        return priv->wsurface != nullptr;
    }

    std::shared_ptr<wf::xwayland_unmanaged_view_node_t> surface_root_node;

  public:
    wayfire_unmanaged_xwayland_view(wlr_xwayland_surface *xww) : wayfire_xwayland_view_base(xww)
    {
        LOGE("new unmanaged xwayland surface ", xw->title, " class: ", xw->class_t,
            " instance: ", xw->instance);

        surface_root_node = std::make_shared<wf::xwayland_unmanaged_view_node_t>(this);
        this->set_surface_root_node(surface_root_node);

        xw->data = this;
        role     = wf::VIEW_ROLE_UNMANAGED;

        on_set_geometry.set_callback([&] (void*) { update_geometry_from_xsurface(); });
        on_map.set_callback([&] (void*) { map(xw->surface); });
        on_unmap.set_callback([&] (void*) { unmap(); });

        on_map.connect(&xw->events.map);
        on_unmap.connect(&xw->events.unmap);
        on_set_geometry.connect(&xw->events.set_geometry);
    }

    virtual void initialize() override
    {
        _initialize();
        wf::view_interface_t::initialize();
    }

    void map(wlr_surface *surface) override
    {
        update_geometry_from_xsurface();

        priv->set_mapped(true);
        this->main_surface = std::make_shared<wf::scene::wlr_surface_node_t>(surface, true);
        priv->set_mapped_surface_contents(main_surface);

        /* We update the keyboard focus before emitting the map event, so that
         * plugins can detect that this view can have keyboard focus.
         *
         * Note: only actual override-redirect views should get their focus disabled */
        priv->keyboard_focus_enabled = (!xw->override_redirect ||
            wlr_xwayland_or_surface_wants_focus(xw));

        wf::scene::readd_front(get_output()->node_for_layer(wf::scene::layer::UNMANAGED), get_root_node());

        if (priv->keyboard_focus_enabled)
        {
            get_output()->focus_view(self(), true);
        }

        damage();
        emit_view_map();
    }

    void unmap() override
    {
        damage();
        emit_view_pre_unmap();

        main_surface = nullptr;
        priv->unset_mapped_surface_contents();
        on_surface_commit.disconnect();

        emit_view_unmap();
        priv->set_mapped(false);
    }

    void destroy() override
    {
        on_map.disconnect();
        on_unmap.disconnect();
        on_set_geometry.disconnect();
        wayfire_xwayland_view_base::destroy();
    }

    wf::xw::view_type get_current_impl_type() const override
    {
        return wf::xw::view_type::UNMANAGED;
    }

    std::string get_app_id() override
    {
        return this->app_id;
    }

    std::string get_title() override
    {
        return this->title;
    }

    wlr_surface *get_keyboard_focus_surface() override
    {
        if (is_mapped() && priv->keyboard_focus_enabled)
        {
            return priv->wsurface;
        }

        return NULL;
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

class wayfire_dnd_xwayland_view : public wayfire_unmanaged_xwayland_view
{
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

    void map(wlr_surface *surface) override
    {
        LOGD("Mapping a Xwayland drag icon");
        wayfire_unmanaged_xwayland_view::map(surface);
        wf::scene::readd_front(wf::get_core().scene(), this->get_root_node());
    }

    void unmap() override
    {
        wayfire_unmanaged_xwayland_view::unmap();
        wf::scene::remove_child(this->get_root_node());
    }
};

#endif
