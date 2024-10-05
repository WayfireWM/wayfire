#include <wayfire/view.hpp>
#include <wayfire/scene.hpp>
#include "../core/core-impl.hpp"
#include "core/seat/seat-impl.hpp"
#include "view-impl.hpp"
#include "wayfire/core.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/util.hpp"
#include "wayfire/unstable/wlr-surface-controller.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"

namespace wf
{
class wlr_surface_pointer_interaction_t final : public wf::pointer_interaction_t
{
    wlr_surface *surface;
    wlr_pointer_constraint_v1 *last_constraint = NULL;
    wf::wl_listener_wrapper constraint_destroyed;

    scene::node_t *self;

    // From position relative to current focus to global scene coordinates
    wf::pointf_t get_absolute_position_from_relative(wf::pointf_t relative)
    {
        auto node = self;
        while (node)
        {
            relative = node->to_global(relative);
            node     = node->parent();
        }

        return relative;
    }

    inline static double distance_between_points(const wf::pointf_t& a,
        const wf::pointf_t& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    inline static wf::pointf_t region_closest_point(const wf::region_t& region,
        const wf::pointf_t& ref)
    {
        if (region.empty() || region.contains_pointf(ref))
        {
            return ref;
        }

        auto extents = region.get_extents();
        wf::pointf_t result = {1.0 * extents.x1, 1.0 * extents.y1};

        for (const auto& box : region)
        {
            auto wlr_box = wlr_box_from_pixman_box(box);

            double x, y;
            wlr_box_closest_point(&wlr_box, ref.x, ref.y, &x, &y);
            wf::pointf_t closest = {x, y};

            if (distance_between_points(ref, result) >
                distance_between_points(ref, closest))
            {
                result = closest;
            }
        }

        return result;
    }

    wf::pointf_t constrain_point(wf::pointf_t point)
    {
        point = get_node_local_coords(self, point);
        auto closest = region_closest_point({&this->last_constraint->region}, point);
        closest = get_absolute_position_from_relative(closest);

        return closest;
    }

    // A handler for pointer motion events before they are passed to the scenegraph.
    // Necessary for the implementation of pointer-constraints and relative-pointer.
    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_motion_event>> on_pointer_motion =
        [=] (wf::input_event_signal<wlr_pointer_motion_event> *evv)
    {
        auto ev    = evv->event;
        auto& seat = wf::get_core_impl().seat;

        // First, we send relative pointer motion as in the raw event, so that
        // clients get the correct delta independently of the pointer constraint.
        wlr_relative_pointer_manager_v1_send_relative_motion(
            wf::get_core().protocols.relative_pointer, seat->seat,
            (uint64_t)ev->time_msec * 1000, ev->delta_x, ev->delta_y,
            ev->unaccel_dx, ev->unaccel_dy);

        double dx = ev->delta_x;
        double dy = ev->delta_y;

        if (last_constraint)
        {
            wf::pointf_t gc     = wf::get_core().get_cursor_position();
            wf::pointf_t target = gc;

            switch (last_constraint->type)
            {
              case WLR_POINTER_CONSTRAINT_V1_CONFINED:
                target = constrain_point({gc.x + dx, gc.y + dy});
                break;

              case WLR_POINTER_CONSTRAINT_V1_LOCKED:
                break;
            }

            ev->delta_x = target.x - gc.x;
            ev->delta_y = target.y - gc.y;
        }
    };

    void _check_activate_constraint()
    {
        auto& seat = wf::get_core_impl().seat;
        auto constraint = wlr_pointer_constraints_v1_constraint_for_surface(
            wf::get_core().protocols.pointer_constraints, surface, seat->seat);

        if (constraint == last_constraint)
        {
            return;
        }

        _reset_constraint();
        if (!constraint)
        {
            return;
        }

        constraint_destroyed.set_callback([=] (void*)
        {
            last_constraint = NULL;
            constraint_destroyed.disconnect();
        });

        constraint_destroyed.connect(&constraint->events.destroy);
        wlr_pointer_constraint_v1_send_activated(constraint);
        last_constraint = constraint;
    }

    wf::signal::connection_t<node_recheck_constraints_signal>
    on_recheck_constraints = [=] (node_recheck_constraints_signal *ev)
    {
        _check_activate_constraint();
    };

    void _reset_constraint()
    {
        if (!this->last_constraint)
        {
            return;
        }

        constraint_destroyed.disconnect();
        wlr_pointer_constraint_v1_send_deactivated(last_constraint);
        last_constraint = NULL;
    }

  public:
    wlr_surface_pointer_interaction_t(wlr_surface *surface, wf::scene::node_t *self)
    {
        this->surface = surface;
        this->self    = self;
        self->connect(&on_recheck_constraints);
    }

    void handle_pointer_button(const wlr_pointer_button_event& event) final
    {
        auto& seat = wf::get_core_impl().seat;
        bool drag_was_active = seat->priv->drag_active;
        seat->priv->last_press_release_serial = wlr_seat_pointer_notify_button(seat->seat,
            event.time_msec, event.button, event.state);

        if (drag_was_active != seat->priv->drag_active)
        {
            // Drag and drop ended. We should refocus the current surface, if we
            // still have focus, because we have set the wlroots focus in a
            // different place during DnD.
            auto& core = wf::get_core();
            auto node  = core.scene()->find_node_at(core.get_cursor_position());
            if (node && (node->node.get() == self))
            {
                wlr_seat_pointer_notify_enter(seat->seat, surface,
                    node->local_coords.x, node->local_coords.y);
            }
        }
    }

    void handle_pointer_enter(wf::pointf_t local) final
    {
        auto seat = wf::get_core_impl().get_current_seat();
        wlr_seat_pointer_notify_enter(seat, surface, local.x, local.y);

        _check_activate_constraint();
        wf::xwayland_bring_to_front(surface);
        wf::get_core().connect(&on_pointer_motion);
    }

    void handle_pointer_motion(wf::pointf_t local, uint32_t time_ms) final
    {
        auto& seat = wf::get_core_impl().seat;
        if (seat->priv->drag_active)
        {
            // Special mode: when drag-and-drop is active, we get an implicit
            // grab on the originating node. So, the original node receives all
            // possible events. It then needs to make sure that the correct node
            // receives the event.
            handle_motion_dnd(time_ms);
            return;
        }

        wlr_seat_pointer_notify_motion(seat->seat, time_ms, local.x, local.y);
    }

    void handle_pointer_axis(const wlr_pointer_axis_event& ev) final
    {
        auto seat = wf::get_core_impl().get_current_seat();
        wlr_seat_pointer_notify_axis(seat, ev.time_msec, ev.orientation,
            ev.delta, ev.delta_discrete, ev.source, WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
    }

    void handle_pointer_leave() final
    {
        auto seat = wf::get_core_impl().get_current_seat();
        if (seat->pointer_state.focused_surface == surface)
        {
            // We defocus only if our surface is still focused on the seat.
            wlr_seat_pointer_notify_clear_focus(seat);
        }

        _reset_constraint();
        on_pointer_motion.disconnect();
    }

    // ---------------------------- DnD implementation ---------------------- */
    void handle_motion_dnd(uint32_t time_ms)
    {
        _reset_constraint();
        auto seat  = wf::get_core().get_current_seat();
        auto gc    = wf::get_core().get_cursor_position();
        auto node  = wf::get_core().scene()->find_node_at(gc);
        auto snode = node ? dynamic_cast<scene::wlr_surface_node_t*>(node->node.get()) : nullptr;
        if (snode && snode->get_surface())
        {
            wlr_seat_pointer_notify_enter(seat, snode->get_surface(),
                node->local_coords.x, node->local_coords.y);
            wlr_seat_pointer_notify_motion(seat, time_ms,
                node->local_coords.x, node->local_coords.y);
        }
    }
};
}
