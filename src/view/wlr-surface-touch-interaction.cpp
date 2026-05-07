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
        wf::pointf_t local, input_grab_kind_t grab = input_grab_kind_t::NONE) override
    {
        auto& seat = wf::get_core_impl().seat;
        LOGC(POINTER, "notify touch down to ", surface,
            " finger=", finger_id,
            " local=", local.x, ",", local.y,
            " grab=", (int)grab);
        seat->priv->last_press_release_serial =
            wlr_seat_touch_notify_down(seat->seat, surface, time_ms, finger_id, local.x, local.y);

        if ((grab == input_grab_kind_t::NONE) && !seat->priv->drag_active)
        {
            wf::xwayland_bring_to_front(surface);
        }
    }

    void handle_touch_up(uint32_t time_ms, int finger_id, wf::pointf_t,
        input_grab_kind_t grab = input_grab_kind_t::NONE) override
    {
        auto& seat = wf::get_core_impl().seat;
        LOGC(POINTER, "notify touch up to ", surface,
            " finger=", finger_id,
            " grab=", (int)grab);
        wlr_seat_touch_notify_up(seat->seat, time_ms, finger_id);
        // FIXME: wlroots does not give us the serial for touch up yet, so we just reset it to something
        // invalid.
        seat->priv->last_press_release_serial = 0;
    }

    void handle_touch_motion(uint32_t time_ms, int finger_id,
        wf::pointf_t local, input_grab_kind_t grab = input_grab_kind_t::NONE) override
    {
        auto& seat = wf::get_core_impl().seat;
        if (grab == input_grab_kind_t::DND)
        {
            auto point = wlr_seat_touch_get_point(seat->seat, finger_id);
            if (!point || (point->surface != surface))
            {
                LOGC(POINTER, "refocus touch point on DnD motion for ", surface,
                    " finger=", finger_id,
                    " local=", local.x, ",", local.y,
                    " point_surface=", point ? point->surface : nullptr);
                wlr_seat_touch_point_focus(seat->seat, surface, time_ms,
                    finger_id, local.x, local.y);
            }
        }

        LOGC(POINTER, "notify touch motion to ", surface,
            " finger=", finger_id,
            " local=", local.x, ",", local.y,
            " grab=", (int)grab);
        wlr_seat_touch_notify_motion(seat->seat, time_ms, finger_id, local.x, local.y);
    }

    bool can_retarget_touch_grab(input_grab_kind_t kind,
        nonstd::observer_ptr<scene::node_t> new_target, int,
        wf::pointf_t) override
    {
        if ((kind != input_grab_kind_t::DND) || !new_target)
        {
            return false;
        }

        return dynamic_cast<scene::wlr_surface_node_t*>(new_target.get()) != nullptr;
    }
};
}
