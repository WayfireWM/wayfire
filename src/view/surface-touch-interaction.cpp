#include <wayfire/view.hpp>
#include <wayfire/scene.hpp>
#include "../core/seat/input-manager.hpp"
#include "../core/core-impl.hpp"
#include <wayfire/compositor-surface.hpp>
#include "view-impl.hpp"

namespace wf
{
class surface_touch_interaction_t final : public wf::touch_interaction_t
{
    wf::surface_interface_t *surface;
    wayfire_view get_view() const
    {
        auto view = dynamic_cast<view_interface_t*>(surface->get_main_surface());
        assert(view);
        return view->self();
    }

  public:
    surface_touch_interaction_t(wf::surface_interface_t *si)
    {
        this->surface = si;
    }

    void handle_touch_down(uint32_t time_ms, int finger_id,
        wf::pointf_t position) override
    {
        auto& seat = wf::get_core_impl().seat;
        auto local = get_surface_relative_coords(surface, position);

        if (auto cs = compositor_surface_from_surface(surface))
        {
            if (finger_id == 0)
            {
                cs->on_touch_down(local.x, local.y);
            }
        } else
        {
            wlr_seat_touch_notify_down(seat->seat, surface->get_wlr_surface(),
                time_ms, finger_id, local.x, local.y);

            if (!seat->drag_active)
            {
                wf::xwayland_bring_to_front(surface->get_wlr_surface());
            }
        }
    }

    void handle_touch_up(uint32_t time_ms, int finger_id) override
    {
        auto seat = wf::get_core().get_current_seat();
        if (auto cs = compositor_surface_from_surface(surface))
        {
            if (finger_id == 0)
            {
                cs->on_touch_up();
            }
        } else
        {
            wlr_seat_touch_notify_up(seat, time_ms, finger_id);
        }
    }

    void handle_touch_motion(uint32_t time_ms, int finger_id,
        wf::pointf_t position) override
    {
        auto& seat = wf::get_core_impl().seat;
        if (seat->drag_active)
        {
            auto node = wf::get_core().scene()->find_node_at(position);
            if (node && node->surface && node->surface->get_wlr_surface())
            {
                wlr_seat_touch_point_focus(seat->seat,
                    node->surface->get_wlr_surface(), time_ms,
                    finger_id, node->local_coords.x, node->local_coords.y);
                wlr_seat_touch_notify_motion(seat->seat, time_ms,
                    finger_id, node->local_coords.x, node->local_coords.y);
            } else
            {
                wlr_seat_touch_point_clear_focus(seat->seat, time_ms, finger_id);
            }

            return;
        }

        auto local = get_surface_relative_coords(surface, position);
        if (auto cs = compositor_surface_from_surface(surface))
        {
            if (finger_id == 0)
            {
                cs->on_touch_motion(local.x, local.y);
            }
        } else
        {
            wlr_seat_touch_notify_motion(seat->seat, time_ms,
                finger_id, local.x, local.y);
        }
    }
};
}
