#include <wayfire/scene.hpp>
#include "../core/core-impl.hpp"
#include "../core/seat/seat-impl.hpp"
#include "view-impl.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"
#include "wayfire/geometry.hpp"

namespace wf
{
class wlr_surface_touch_interaction_t final : public wf::touch_interaction_t
{
    wlr_surface *surface;

  public:
    wlr_surface_touch_interaction_t(wlr_surface *surface)
    {
        this->surface = surface;
    }

    void handle_touch_down(uint32_t time_ms, int finger_id,
        wf::pointf_t local) override
    {
        auto& seat = wf::get_core_impl().seat;
        seat->priv->last_press_release_serial =
            wlr_seat_touch_notify_down(seat->seat, surface, time_ms, finger_id, local.x, local.y);

        if (!seat->priv->drag_active)
        {
            wf::xwayland_bring_to_front(surface);
        }
    }

    void handle_touch_up(uint32_t time_ms, int finger_id, wf::pointf_t) override
    {
        auto& seat = wf::get_core_impl().seat;
        wlr_seat_touch_notify_up(seat->seat, time_ms, finger_id);
        // FIXME: wlroots does not give us the serial for touch up yet, so we just reset it to something
        // invalid.
        seat->priv->last_press_release_serial = 0;
    }

    void handle_touch_motion(uint32_t time_ms, int finger_id,
        wf::pointf_t local) override
    {
        auto& seat = wf::get_core_impl().seat;
        if (seat->priv->drag_active)
        {
            auto gc    = wf::get_core().get_touch_position(finger_id);
            auto node  = wf::get_core().scene()->find_node_at(gc);
            auto snode = node ? dynamic_cast<scene::wlr_surface_node_t*>(node->node.get()) : nullptr;
            if (snode && snode->get_surface())
            {
                wlr_seat_touch_point_focus(seat->seat, snode->get_surface(), time_ms, finger_id,
                    node->local_coords.x, node->local_coords.y);
                wlr_seat_touch_notify_motion(seat->seat, time_ms, finger_id,
                    node->local_coords.x, node->local_coords.y);
            } else
            {
                wlr_seat_touch_point_clear_focus(seat->seat, time_ms, finger_id);
            }

            return;
        }

        wlr_seat_touch_notify_motion(seat->seat, time_ms, finger_id, local.x, local.y);
    }
};
}
