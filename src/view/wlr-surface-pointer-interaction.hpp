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
#include <array>

#include "xdg-shell.hpp"

namespace wf
{
class wlr_surface_pointer_interaction_t final : public wf::pointer_interaction_t
{
    wlr_surface *surface;
    wlr_pointer_constraint_v1 *last_constraint = NULL;
    wf::wl_listener_wrapper constraint_destroyed;

    scene::node_t *self;

    wf::view_interface_t *get_view() const
    {
        if (!self)
        {
            return nullptr;
        }

        return wf::node_to_view(self->shared_from_this()).get();
    }

    static wf::view_interface_t *get_popup_grab_root(wf::view_interface_t *view)
    {
        wf::view_interface_t *current = view;
        while (auto as_popup = dynamic_cast<wayfire_xdg_popup*>(current))
        {
            auto parent = as_popup->popup_parent.lock();
            if (!parent)
            {
                break;
            }

            current = parent.get();
        }

        return current;
    }

    bool can_retarget_implicit_popup_grab(nonstd::observer_ptr<scene::node_t> new_target) const
    {
        auto current_view = get_view();
        if (!current_view)
        {
            return false;
        }

        auto new_view = wf::node_to_view(new_target.get());
        if (!new_view)
        {
            return false;
        }

        if (new_view.get() == current_view)
        {
            return false;
        }

        return get_popup_grab_root(current_view) == get_popup_grab_root(new_view.get());
    }

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
            _warp_to_cursor_hint();
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

    void _warp_to_cursor_hint()
    {
        if (!this->last_constraint || !this->last_constraint->current.cursor_hint.enabled)
        {
            return;
        }

        wf::pointf_t local = wf::pointf_t{
            (double)this->last_constraint->current.cursor_hint.x,
            (double)this->last_constraint->current.cursor_hint.y
        };

        auto global = get_absolute_position_from_relative(local);
        wf::get_core().warp_cursor(global);
        wlr_seat_pointer_warp(wf::get_core().seat->seat, local.x, local.y);
    }

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

    void handle_pointer_button(const wlr_pointer_button_event& event,
        input_grab_kind_t grab = input_grab_kind_t::NONE) final
    {
        auto& seat = wf::get_core_impl().seat;
        seat->priv->last_press_release_serial = wlr_seat_pointer_notify_button(seat->seat,
            event.time_msec, event.button, event.state);
        LOGC(POINTER, "notify button to ", surface, " state=", event.state,
            " button=", event.button, " grab=", (int)grab,
            " serial=", seat->priv->last_press_release_serial);

        if (grab == input_grab_kind_t::DND)
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

    void handle_pointer_enter(wf::pointf_t local,
        input_grab_kind_t grab = input_grab_kind_t::NONE) final
    {
        auto seat = wf::get_core_impl().get_current_seat();
        std::array<wlr_seat_pointer_button, WLR_POINTER_BUTTONS_CAP> saved_buttons = {};
        auto saved_button_count = seat->pointer_state.button_count;
        std::copy_n(seat->pointer_state.buttons, saved_button_count, saved_buttons.begin());
        LOGC(POINTER, "notify enter to ", surface,
            " local=", local.x, ",", local.y,
            " grab=", (int)grab,
            " focused_before=", seat->pointer_state.focused_surface,
            " buttons=", saved_button_count);
        wlr_seat_pointer_notify_enter(seat, surface, local.x, local.y);

        if ((grab == input_grab_kind_t::IMPLICIT) || (grab == input_grab_kind_t::DND))
        {
            std::copy_n(saved_buttons.begin(), saved_button_count,
                seat->pointer_state.buttons);
            seat->pointer_state.button_count = saved_button_count;
            LOGC(POINTER, "restore pressed state after enter for ", surface,
                " button_count=", saved_button_count,
                " grab=", (int)grab);
        }

        if ((grab == input_grab_kind_t::NONE) || (grab == input_grab_kind_t::IMPLICIT))
        {
            _check_activate_constraint();
            wf::xwayland_bring_to_front(surface);
            wf::get_core().connect(&on_pointer_motion);
        }
    }

    bool can_retarget_pointer_grab(input_grab_kind_t kind,
        nonstd::observer_ptr<scene::node_t> new_target, wf::pointf_t) final
    {
        if (!new_target)
        {
            return false;
        }

        if (kind == input_grab_kind_t::DND)
        {
            return dynamic_cast<scene::wlr_surface_node_t*>(new_target.get()) != nullptr;
        }

        if (kind != input_grab_kind_t::IMPLICIT)
        {
            return false;
        }

        return can_retarget_implicit_popup_grab(new_target);
    }

    void handle_pointer_motion(wf::pointf_t local, uint32_t time_ms,
        input_grab_kind_t grab = input_grab_kind_t::NONE) final
    {
        auto& seat = wf::get_core_impl().seat;
        if ((grab == input_grab_kind_t::DND) &&
            (seat->seat->pointer_state.focused_surface != surface))
        {
            LOGC(POINTER, "re-enter on DnD motion for ", surface,
                " local=", local.x, ",", local.y,
                " focused_before=", seat->seat->pointer_state.focused_surface);
            handle_pointer_enter(local, grab);
        }

        LOGC(POINTER, "notify motion to ", surface,
            " local=", local.x, ",", local.y,
            " time=", time_ms,
            " grab=", (int)grab,
            " focused=", seat->seat->pointer_state.focused_surface);
        wlr_seat_pointer_notify_motion(seat->seat, time_ms, local.x, local.y);
    }

    void handle_pointer_axis(const wlr_pointer_axis_event& ev) final
    {
        auto seat = wf::get_core_impl().get_current_seat();
        wlr_seat_pointer_notify_axis(seat, ev.time_msec, ev.orientation,
            ev.delta, ev.delta_discrete, ev.source, WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
    }

    void handle_pointer_leave(input_grab_kind_t grab = input_grab_kind_t::NONE) final
    {
        auto seat = wf::get_core_impl().get_current_seat();
        if ((grab == input_grab_kind_t::NONE) &&
            (seat->pointer_state.focused_surface == surface))
        {
            // We defocus only if our surface is still focused on the seat.
            LOGC(POINTER, "notify clear focus from ", surface,
                " grab=", (int)grab);
            wlr_seat_pointer_notify_clear_focus(seat);
        } else
        {
            LOGC(POINTER, "skip clear focus from ", surface,
                " grab=", (int)grab,
                " focused=", seat->pointer_state.focused_surface);
        }

        _warp_to_cursor_hint();
        _reset_constraint();
        on_pointer_motion.disconnect();
    }
};
}
