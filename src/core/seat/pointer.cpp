#include "pointer.hpp"
#include "cursor.hpp"
#include "pointing-device.hpp"
#include "input-manager.hpp"
#include "wayfire/signal-definitions.hpp"

#include <wayfire/util/log.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/compositor-surface.hpp>

wf::pointer_t::pointer_t(nonstd::observer_ptr<wf::input_manager_t> input,
    nonstd::observer_ptr<seat_t> seat)
{
    this->input = input;
    this->seat  = seat;
    on_surface_map_state_change.set_callback([=] (auto surface)
    {
        if (surface && (grabbed_surface == surface) && !surface->is_mapped())
        {
            grab_surface(nullptr);
        } else
        {
            update_cursor_position(get_current_time(), false);
        }
    });

    on_views_updated = [&] (wf::signal_data_t*)
    {
        update_cursor_position(get_current_time(), false);
    };
    wf::get_core().connect_signal("output-stack-order-changed", &on_views_updated);
    wf::get_core().connect_signal("view-geometry-changed", &on_views_updated);
}

wf::pointer_t::~pointer_t()
{}

bool wf::pointer_t::has_pressed_buttons() const
{
    return this->count_pressed_buttons > 0;
}

/* ------------------------- Cursor focus functions ------------------------- */
void wf::pointer_t::set_enable_focus(bool enabled)
{
    this->focus_enabled_count += enabled ? 1 : -1;
    if (focus_enabled_count > 1)
    {
        LOGI("LogicalPointer enabled more times than disabled?");
    }

    // reset grab
    if (!focus_enabled())
    {
        grab_surface(nullptr);
        this->update_cursor_focus(nullptr, {0.0, 0.0});
    } else
    {
        update_cursor_position(get_current_time(), false);
    }
}

bool wf::pointer_t::focus_enabled() const
{
    return this->focus_enabled_count > 0;
}

void wf::pointer_t::update_cursor_position(uint32_t time_msec, bool real_update)
{
    wf::pointf_t gc = seat->cursor->get_cursor_position();

    wf::pointf_t local = {0.0, 0.0};
    wf::surface_interface_t *new_focus = nullptr;
    /* If we have a grabbed surface, but no drag, we want to continue sending
     * events to the grabbed surface, even if the pointer goes outside of it.
     * This enables Xwayland DnD to work correctly, and also lets the user for
     * ex. grab a scrollbar and move their mouse freely.
     *
     * Notice in case of active wayland DnD we need to send events to the
     * surfaces which are actually under the mouse */
    if (grabbed_surface && !seat->drag_active)
    {
        new_focus = grabbed_surface;
        local     = get_surface_relative_coords(new_focus, gc);
    } else if (this->focus_enabled())
    {
        new_focus = input->input_surface_at(gc, local);
        update_cursor_focus(new_focus, local);

        /* We switched focus, so send motion event in any case, so that the
         * new focus knows where the pointer is */
        real_update = true;
    }

    if (real_update)
    {
        this->send_motion(time_msec, local);
    }

    seat->update_drag_icon();
}

void wf::pointer_t::update_cursor_focus(
    wf::surface_interface_t *focus, wf::pointf_t local)
{
    if (focus && !input->can_focus_surface(focus))
    {
        return;
    }

    if (focus && !this->focus_enabled())
    {
        return;
    }

    bool focus_change = (cursor_focus != focus);
    if (focus_change)
    {
        LOGD("change cursor focus ", cursor_focus, " -> ", focus);
    }

    /* Send leave to old focus if compositor surface */
    wf::compositor_surface_t *compositor_surface =
        compositor_surface_from_surface(cursor_focus);
    if (compositor_surface && focus_change)
    {
        compositor_surface->on_pointer_leave();
    }

    // Clear currently sent buttons when switching focus
    // However, if we are in drag-and-drop mode, do not release
    // buttons since otherwise we'll cancel DnD
    if (cursor_focus && focus_change && !seat->drag_active)
    {
        for (auto button : this->currently_sent_buttons)
        {
            wlr_seat_pointer_notify_button(seat->seat,
                get_current_time(), button, WLR_BUTTON_RELEASED);
        }

        currently_sent_buttons.clear();
    }

    cursor_focus = focus;
    seat->ensure_input_surface(focus);

    wlr_surface *next_focus_wlr_surface = nullptr;
    if (focus && focus->get_wlr_surface())
    {
        next_focus_wlr_surface = focus->get_wlr_surface();
        wlr_seat_pointer_notify_enter(seat->seat, next_focus_wlr_surface,
            local.x, local.y);
    } else
    {
        wlr_seat_pointer_notify_clear_focus(seat->seat);
    }

    if (focus_change &&
        (compositor_surface = compositor_surface_from_surface(focus)))
    {
        compositor_surface->on_pointer_enter(local.x, local.y);
    }

    if (focus_change)
    {
        wlr_pointer_constraint_v1 *constraint = NULL;
        if (next_focus_wlr_surface)
        {
            constraint = wlr_pointer_constraints_v1_constraint_for_surface(
                wf::get_core().protocols.pointer_constraints,
                next_focus_wlr_surface, seat->seat);
        }

        set_pointer_constraint(constraint);
    }

    if (!cursor_focus)
    {
        wf::get_core().set_cursor("default");
    }
}

wf::surface_interface_t*wf::pointer_t::get_focus() const
{
    return this->cursor_focus;
}

/* --------------------- Pointer constraints implementation ----------------- */
wlr_pointer_constraint_v1*wf::pointer_t::get_active_pointer_constraint()
{
    return this->active_pointer_constraint;
}

wf::pointf_t wf::pointer_t::get_absolute_position_from_relative(
    wf::pointf_t relative)
{
    auto view =
        (wf::view_interface_t*)(this->cursor_focus->get_main_surface());

    auto output_geometry = view->get_output_geometry();
    wf::point_t origin   = {output_geometry.x, output_geometry.y};

    for (auto& surf : view->enumerate_surfaces(origin))
    {
        if (surf.surface == this->cursor_focus)
        {
            relative.x += surf.position.x;
            relative.y += surf.position.y;
        }
    }

    relative = view->transform_point(relative);
    auto output = view->get_output()->get_layout_geometry();

    return {relative.x + output.x, relative.y + output.y};
}

static double distance_between_points(const wf::pointf_t& a, const wf::pointf_t& b)
{
    return std::sqrt(1.0 * (a.x - b.x) * (a.x - b.x) +
        1.0 * (a.y - b.y) * (a.y - b.y));
}

static wf::pointf_t region_closest_point(const wf::region_t& region,
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

wf::pointf_t wf::pointer_t::constrain_point(wf::pointf_t point)
{
    point = get_surface_relative_coords(this->cursor_focus, point);
    auto closest = region_closest_point(this->constraint_region, point);
    closest = get_absolute_position_from_relative(closest);

    return closest;
}

void wf::pointer_t::set_pointer_constraint(
    wlr_pointer_constraint_v1 *constraint, bool last_destroyed)
{
    if (constraint == this->active_pointer_constraint)
    {
        return;
    }

    /* First set the constraint to the new constraint.
     * send_deactivated might cause destruction of the active constraint,
     * and then before we've finished this request we'd get another to reset
     * the constraint to NULL.
     *
     * XXX: a race is still possible if we directly switch from one constraint
     * to another, and the first one gets destroyed. This is however almost
     * impossible, since a constraint keeps the cursor inside its surface, so
     * the only way to cancel this would be to either cancel the constraint by
     * activating a plugin or when the constraint itself gets destroyed. In both
     * cases, we first get a set_pointer_constraint(NULL) request */
    auto last_constraint = this->active_pointer_constraint;
    this->active_pointer_constraint = constraint;

    if (last_constraint && !last_destroyed)
    {
        wlr_pointer_constraint_v1_send_deactivated(last_constraint);
        // TODO: restore cursor position from the constraint hint
    }

    this->constraint_region.clear();
    if (!constraint)
    {
        return;
    }

    wlr_pointer_constraint_v1_send_activated(constraint);
    if (constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED)
    {
        this->constraint_region = wf::region_t{&constraint->region};
    }

    if (this->cursor_focus)
    {
        auto current = seat->cursor->get_cursor_position();
        seat->cursor->warp_cursor(constrain_point(current));
    }
}

/* -------------------------- Implicit grab --------------------------------- */
void wf::pointer_t::grab_surface(wf::surface_interface_t *surface)
{
    if (surface == grabbed_surface)
    {
        return;
    }

    if (surface)
    {
        /* Start a new grab */
        this->grabbed_surface = surface;

        return;
    }

    /* End grab */
    grabbed_surface = nullptr;
    update_cursor_position(get_current_time(), false);
}

/* ----------------------- Input event processing --------------------------- */
void wf::pointer_t::handle_pointer_button(wlr_pointer_button_event *ev,
    input_event_processing_mode_t mode)
{
    seat->break_mod_bindings();
    bool handled_in_binding = (mode != input_event_processing_mode_t::FULL);

    if (ev->state == WLR_BUTTON_PRESSED)
    {
        count_pressed_buttons++;
        if (count_pressed_buttons == 1)
        {
            /* Focus only the first click, since then we also start an implicit
             * grab, and we don't want to suddenly change the output */
            auto gc     = seat->cursor->get_cursor_position();
            auto output =
                wf::get_core().output_layout->get_output_at(gc.x, gc.y);
            wf::get_core().focus_output(output);
        }

        handled_in_binding |= input->get_active_bindings().handle_button(
            wf::buttonbinding_t{seat->get_modifiers(), ev->button});
    } else
    {
        count_pressed_buttons--;
    }

    send_button(ev, handled_in_binding);
    if (!handled_in_binding)
    {
        check_implicit_grab();
    }
}

void wf::pointer_t::check_implicit_grab()
{
    /* start a button held grab, so that the window will receive all the
     * subsequent events, no matter what happens */
    if ((count_pressed_buttons == 1) && cursor_focus)
    {
        grab_surface(cursor_focus);
    }

    /* end the button held grab. We need to to this here after we have send
     * the last button release event, so that buttons don't get stuck in clients */
    if (count_pressed_buttons == 0)
    {
        grab_surface(nullptr);
    }
}

void wf::pointer_t::send_button(wlr_pointer_button_event *ev, bool has_binding)
{
    if (input->active_grab)
    {
        if (input->active_grab->callbacks.pointer.button)
        {
            input->active_grab->callbacks.pointer.button(ev->button, ev->state);
        }

        return;
    }

    /* Clients do not receive buttons for bindings */
    if (has_binding || !cursor_focus)
    {
        return;
    }

    if (ev->state == WLR_BUTTON_PRESSED)
    {
        this->currently_sent_buttons.insert(ev->button);
    } else
    {
        if (!this->currently_sent_buttons.count(ev->button))
        {
            // Ignore buttons which the client has not received.
            // These are potentially buttons which were grabbed.
            return;
        }

        this->currently_sent_buttons.erase(
            currently_sent_buttons.find(ev->button));
    }

    auto custom = compositor_surface_from_surface(cursor_focus);
    if (custom)
    {
        custom->on_pointer_button(ev->button, ev->state);
    }

    wlr_seat_pointer_notify_button(seat->seat, ev->time_msec,
        ev->button, ev->state);
}

void wf::pointer_t::send_motion(uint32_t time_msec, wf::pointf_t local)
{
    if (input->input_grabbed())
    {
        auto oc = wf::get_core().get_active_output()->get_cursor_position();
        if (input->active_grab->callbacks.pointer.motion)
        {
            input->active_grab->callbacks.pointer.motion(oc.x, oc.y);
        }
    }

    auto compositor_surface =
        compositor_surface_from_surface(this->cursor_focus);
    if (compositor_surface)
    {
        compositor_surface->on_pointer_motion(local.x, local.y);
    } else
    {
        wlr_seat_pointer_notify_motion(
            seat->seat, time_msec, local.x, local.y);
    }
}

void wf::pointer_t::handle_pointer_motion(wlr_pointer_motion_event *ev,
    input_event_processing_mode_t mode)
{
    if (input->input_grabbed() &&
        input->active_grab->callbacks.pointer.relative_motion)
    {
        input->active_grab->callbacks.pointer.relative_motion(ev);
    }

    // send relative motion
    wlr_relative_pointer_manager_v1_send_relative_motion(
        wf::get_core().protocols.relative_pointer, seat->seat,
        (uint64_t)ev->time_msec * 1000, ev->delta_x, ev->delta_y,
        ev->unaccel_dx, ev->unaccel_dy);

    double dx = ev->delta_x;
    double dy = ev->delta_y;

    // confine inside constraints
    if (this->active_pointer_constraint && this->cursor_focus)
    {
        if (constraint_region.empty())
        {
            /* Empty region */
            dx = dy = 0;
        } else
        {
            // next coordinates
            auto gc     = seat->cursor->get_cursor_position();
            auto target = constrain_point({gc.x + dx, gc.y + dy});

            dx = target.x - gc.x;
            dy = target.y - gc.y;
        }
    }

    /* XXX: maybe warp directly? */
    wlr_cursor_move(seat->cursor->cursor, &ev->pointer->base, dx, dy);
    update_cursor_position(ev->time_msec);
}

void wf::pointer_t::handle_pointer_motion_absolute(
    wlr_pointer_motion_absolute_event *ev, input_event_processing_mode_t mode)
{
    // next coordinates
    double cx, cy;
    wlr_cursor_absolute_to_layout_coords(seat->cursor->cursor, &ev->pointer->base,
        ev->x, ev->y, &cx, &cy);

    // send relative motion
    double dx = cx - seat->cursor->cursor->x;
    double dy = cy - seat->cursor->cursor->y;
    wlr_relative_pointer_manager_v1_send_relative_motion(
        wf::get_core().protocols.relative_pointer, seat->seat,
        (uint64_t)ev->time_msec * 1000, dx, dy, dx, dy);

    // check constraints
    if (this->active_pointer_constraint && this->cursor_focus)
    {
        auto local = get_surface_relative_coords(this->cursor_focus, {cx, cy});
        if (!constraint_region.contains_pointf(local))
        {
            return;
        }
    }

    // TODO: indirection via wf_cursor
    wlr_cursor_warp_closest(seat->cursor->cursor, NULL, cx, cy);
    update_cursor_position(ev->time_msec);
}

void wf::pointer_t::handle_pointer_axis(wlr_pointer_axis_event *ev,
    input_event_processing_mode_t mode)
{
    bool handled_in_binding = input->get_active_bindings().handle_axis(
        seat->get_modifiers(), ev);
    seat->break_mod_bindings();

    if (input->active_grab)
    {
        if (input->active_grab->callbacks.pointer.axis)
        {
            input->active_grab->callbacks.pointer.axis(ev);
        }

        return;
    }

    /* Do not send scroll events to clients if an axis binding has used up the
     * event */
    if (handled_in_binding)
    {
        return;
    }

    /* Calculate speed settings */
    double mult = ev->source == WLR_AXIS_SOURCE_FINGER ?
        wf::pointing_device_t::config.touchpad_scroll_speed :
        wf::pointing_device_t::config.mouse_scroll_speed;

    wlr_seat_pointer_notify_axis(seat->seat, ev->time_msec, ev->orientation,
        mult * ev->delta, mult * ev->delta_discrete, ev->source);
}

void wf::pointer_t::handle_pointer_swipe_begin(wlr_pointer_swipe_begin_event *ev,
    input_event_processing_mode_t mode)
{
    wlr_pointer_gestures_v1_send_swipe_begin(
        wf::get_core().protocols.pointer_gestures, seat->seat,
        ev->time_msec, ev->fingers);
}

void wf::pointer_t::handle_pointer_swipe_update(
    wlr_pointer_swipe_update_event *ev, input_event_processing_mode_t mode)
{
    wlr_pointer_gestures_v1_send_swipe_update(
        wf::get_core().protocols.pointer_gestures, seat->seat,
        ev->time_msec, ev->dx, ev->dy);
}

void wf::pointer_t::handle_pointer_swipe_end(wlr_pointer_swipe_end_event *ev,
    input_event_processing_mode_t mode)
{
    wlr_pointer_gestures_v1_send_swipe_end(
        wf::get_core().protocols.pointer_gestures, seat->seat,
        ev->time_msec, ev->cancelled);
}

void wf::pointer_t::handle_pointer_pinch_begin(wlr_pointer_pinch_begin_event *ev,
    input_event_processing_mode_t mode)
{
    wlr_pointer_gestures_v1_send_pinch_begin(
        wf::get_core().protocols.pointer_gestures, seat->seat,
        ev->time_msec, ev->fingers);
}

void wf::pointer_t::handle_pointer_pinch_update(
    wlr_pointer_pinch_update_event *ev, input_event_processing_mode_t mode)
{
    wlr_pointer_gestures_v1_send_pinch_update(
        wf::get_core().protocols.pointer_gestures, seat->seat,
        ev->time_msec, ev->dx, ev->dy, ev->scale, ev->rotation);
}

void wf::pointer_t::handle_pointer_pinch_end(wlr_pointer_pinch_end_event *ev,
    input_event_processing_mode_t mode)
{
    wlr_pointer_gestures_v1_send_pinch_end(
        wf::get_core().protocols.pointer_gestures, seat->seat,
        ev->time_msec, ev->cancelled);
}

void wf::pointer_t::handle_pointer_frame()
{
    wlr_seat_pointer_notify_frame(seat->seat);
}
