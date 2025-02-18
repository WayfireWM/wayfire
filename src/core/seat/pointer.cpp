#include "pointer.hpp"
#include <wayfire/bindings-repository.hpp>
#include "cursor.hpp"
#include "pointing-device.hpp"
#include "input-manager.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"

#include <wayfire/debug.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>

wf::pointer_t::pointer_t(nonstd::observer_ptr<wf::input_manager_t> input,
    nonstd::observer_ptr<seat_t> seat)
{
    this->input = input;
    this->seat  = seat;

    on_root_node_updated = [=] (wf::scene::root_node_update_signal *data)
    {
        if (!(data->flags & wf::scene::update_flag::INPUT_STATE))
        {
            return;
        }

        if (grabbed_node && !is_grabbed_node_alive(grabbed_node))
        {
            grab_surface(nullptr);
        }

        update_cursor_position(get_current_time());
    };

    wf::get_core().scene()->connect(&on_root_node_updated);
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
        this->update_cursor_focus(nullptr);
    } else
    {
        update_cursor_position(get_current_time());
    }
}

bool wf::pointer_t::focus_enabled() const
{
    return this->focus_enabled_count > 0;
}

void wf::pointer_t::update_cursor_position(int64_t time_msec)
{
    wf::pointf_t gc = seat->priv->cursor->get_cursor_position();

    /* If we have a grabbed surface, but no drag, we want to continue sending
     * events to the grabbed surface, even if the pointer goes outside of it.
     * This enables Xwayland DnD to work correctly, and also lets the user for
     * ex. grab a scrollbar and move their mouse freely. */
    if (!grabbed_node && this->focus_enabled())
    {
        const auto& scene = wf::get_core().scene();
        auto isec = scene->find_node_at(gc);
        update_cursor_focus(isec ? isec->node->shared_from_this() : nullptr);
    }

    this->send_motion(time_msec);
    seat->priv->update_drag_icon();
}

void wf::pointer_t::send_leave_to_focus(wf::scene::node_ptr old_focus)
{
    if (old_focus)
    {
        if (!old_focus->wants_raw_input())
        {
            // Make a copy of sent buttons: it could happen that we switch focus multiple times if we have
            // multiple grabs on multiple outputs.
            auto send_release = this->currently_sent_buttons;
            for (auto button : send_release)
            {
                LOGC(POINTER, "force-release button ", button);
                wlr_pointer_button_event event;
                event.pointer   = NULL;
                event.button    = button;
                event.state     = WL_POINTER_BUTTON_STATE_RELEASED;
                event.time_msec = wf::get_current_time();
                old_focus->pointer_interaction().handle_pointer_button(event);
            }
        }

        old_focus->pointer_interaction().handle_pointer_leave();
        this->last_focus_coords = {};
    }
}

void wf::pointer_t::transfer_grab(scene::node_ptr node)
{
    if (node == cursor_focus)
    {
        LOGC(POINTER, "transfer grab ", cursor_focus.get(), " -> ", node.get(), ": do nothing");
        // Node might already be focused, in case for example there was no input surface when the grab node
        // was added to the scenegraph.
        return;
    }

    LOGC(POINTER, "transfer grab ", cursor_focus.get(), " -> ", node.get());
    auto old_focus = std::move(cursor_focus);
    cursor_focus = node;
    send_leave_to_focus(old_focus);

    // Send pointer_enter to the grab
    send_enter_to_focus();
    if (!node->wants_raw_input())
    {
        currently_sent_buttons.clear();
    }

    if (currently_sent_buttons.size())
    {
        grabbed_node = node;
    } else
    {
        grabbed_node = nullptr;
    }
}

void wf::pointer_t::send_enter_to_focus()
{
    auto gc    = wf::get_core().get_cursor_position();
    auto local = get_node_local_coords(cursor_focus.get(), gc);
    cursor_focus->pointer_interaction().handle_pointer_enter(local);
    this->last_focus_coords = local;
}

void wf::pointer_t::update_cursor_focus(wf::scene::node_ptr new_focus)
{
    if (cursor_focus == new_focus)
    {
        return;
    }

    LOGC(POINTER, "Change cursor focus ", cursor_focus.get(), " -> ", new_focus.get());
    auto old_focus = std::move(cursor_focus);
    cursor_focus = new_focus;

    send_leave_to_focus(old_focus);
    currently_sent_buttons.clear();

    if (cursor_focus)
    {
        send_enter_to_focus();
    } else
    {
        // If there is focused surface, we should reset the cursor image to
        // avoid the last cursor image getting stuck outside of its surface.
        wf::get_core().set_cursor("default");
    }

    wf::pointer_focus_changed_signal ev;
    ev.new_focus = cursor_focus;
    wf::get_core().emit(&ev);
}

wf::scene::node_ptr wf::pointer_t::get_focus() const
{
    return this->cursor_focus;
}

/* -------------------------- Implicit grab --------------------------------- */
void wf::pointer_t::grab_surface(wf::scene::node_ptr node)
{
    if (node == grabbed_node)
    {
        return;
    }

    if (node)
    {
        /* Start a new grab */
        this->grabbed_node = node;
        return;
    }

    /* End grab */
    grabbed_node = nullptr;
    update_cursor_position(get_current_time());
}

/* ----------------------- Input event processing --------------------------- */
void wf::pointer_t::handle_pointer_button(wlr_pointer_button_event *ev,
    input_event_processing_mode_t mode)
{
    seat->priv->break_mod_bindings();
    bool handled_in_binding = (mode != input_event_processing_mode_t::FULL);

    if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        count_pressed_buttons++;
        if (count_pressed_buttons == 1)
        {
            /* Focus only the first click, since then we also start an implicit
             * grab, and we don't want to suddenly change the output */
            auto gc     = seat->priv->cursor->get_cursor_position();
            auto output = wf::get_core().output_layout->get_output_at(gc.x, gc.y);
            seat->focus_output(output);
        }

        handled_in_binding |= wf::get_core().bindings->handle_button(
            wf::buttonbinding_t{seat->priv->get_modifiers(), ev->button});
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
    /* Clients do not receive buttons for bindings */
    if (has_binding)
    {
        return;
    }

    if (cursor_focus)
    {
        if ((ev->state == WL_POINTER_BUTTON_STATE_PRESSED) && cursor_focus)
        {
            LOGC(POINTER, "normal button press ", ev->button);
            this->currently_sent_buttons.insert(ev->button);
            cursor_focus->pointer_interaction().handle_pointer_button(*ev);
        } else if ((ev->state == WL_POINTER_BUTTON_STATE_RELEASED) &&
                   (currently_sent_buttons.count(ev->button) || cursor_focus->wants_raw_input()))
        {
            LOGC(POINTER, "normal button release ", ev->button);
            if (currently_sent_buttons.count(ev->button))
            {
                this->currently_sent_buttons.erase(currently_sent_buttons.find(ev->button));
            }

            cursor_focus->pointer_interaction().handle_pointer_button(*ev);
        } else
        {
            LOGC(POINTER, "ignoring button event ", ev->button, " ", ev->state);
            // Ignore buttons which the client has not received.
            // These are potentially buttons which were grabbed.
        }
    } else
    {
        LOGC(POINTER, "ignoring button event (null focus) ", ev->button, " ", ev->state);
    }
}

void wf::pointer_t::send_motion(uint32_t time_msec)
{
    if (cursor_focus)
    {
        auto gc    = wf::get_core().get_cursor_position();
        auto local = get_node_local_coords(cursor_focus.get(), gc);

        if (!last_focus_coords.has_value() ||
            (local.x != last_focus_coords->x) || (local.y != last_focus_coords->y))
        {
            // NB: the pointer motion could go to a grab, which could trigger scenegraph changes, which
            // trigger re-focus. We need to set the last focus coordinates early, so that we break out of
            // infinite loops.
            last_focus_coords = local;
            cursor_focus->pointer_interaction().handle_pointer_motion(local, time_msec);
        }
    }
}

void wf::pointer_t::handle_pointer_motion(wlr_pointer_motion_event *ev,
    input_event_processing_mode_t mode)
{
    /* XXX: maybe warp directly? */
    wlr_cursor_move(seat->priv->cursor->cursor, &ev->pointer->base, ev->delta_x, ev->delta_y);
    update_cursor_position(ev->time_msec);
}

void wf::pointer_t::handle_pointer_motion_absolute(
    wlr_pointer_motion_absolute_event *ev, input_event_processing_mode_t mode)
{
    // next coordinates
    double cx, cy;
    wlr_cursor_absolute_to_layout_coords(seat->priv->cursor->cursor, &ev->pointer->base,
        ev->x, ev->y, &cx, &cy);

    // send relative motion
    double dx = cx - seat->priv->cursor->cursor->x;
    double dy = cy - seat->priv->cursor->cursor->y;
    wlr_relative_pointer_manager_v1_send_relative_motion(
        wf::get_core().protocols.relative_pointer, seat->seat,
        (uint64_t)ev->time_msec * 1000, dx, dy, dx, dy);

    // TODO: indirection via wf_cursor
    wlr_cursor_warp_closest(seat->priv->cursor->cursor, NULL, cx, cy);
    update_cursor_position(ev->time_msec);
}

void wf::pointer_t::handle_pointer_axis(wlr_pointer_axis_event *ev,
    input_event_processing_mode_t mode)
{
    bool handled_in_binding = wf::get_core().bindings->handle_axis(
        seat->priv->get_modifiers(), ev);
    seat->priv->break_mod_bindings();

    /* Do not send scroll events to clients if an axis binding has used up the
     * event */
    if (handled_in_binding)
    {
        return;
    }

    /* Calculate speed settings */
    wf::pointing_device_t *pd = (wf::pointing_device_t*)ev->pointer->base.data;
    double mult = pd->get_scroll_speed(&ev->pointer->base, ev->source == WL_POINTER_AXIS_SOURCE_FINGER);

    ev->delta *= mult;
    ev->delta_discrete *= mult;

    if (cursor_focus)
    {
        cursor_focus->pointer_interaction().handle_pointer_axis(*ev);
    }
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

void wf::pointer_t::handle_pointer_hold_begin(wlr_pointer_hold_begin_event *ev,
    input_event_processing_mode_t mode)
{
    wlr_pointer_gestures_v1_send_hold_begin(
        wf::get_core().protocols.pointer_gestures, seat->seat,
        ev->time_msec, ev->fingers);
}

void wf::pointer_t::handle_pointer_hold_end(wlr_pointer_hold_end_event *ev,
    input_event_processing_mode_t mode)
{
    wlr_pointer_gestures_v1_send_hold_end(
        wf::get_core().protocols.pointer_gestures, seat->seat,
        ev->time_msec, ev->cancelled);
}

void wf::pointer_t::handle_pointer_frame()
{
    wlr_seat_pointer_notify_frame(seat->seat);
}
