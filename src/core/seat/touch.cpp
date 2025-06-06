#include <wayfire/util/log.hpp>
#include <wayfire/bindings-repository.hpp>

#include "touch.hpp"
#include "cursor.hpp"
#include "input-manager.hpp"
#include "../core-impl.hpp"
#include "wayfire/core.hpp"
#include "wayfire/output.hpp"
#include "wayfire/util.hpp"
#include "wayfire/output-layout.hpp"
#include <glm/glm.hpp>

class touch_timer_adapter_t : public wf::touch::timer_interface_t
{
  public:
    wf::wl_timer<false> timer;
    void set_timeout(uint32_t msec, std::function<void()> handler)
    {
        timer.set_timeout(msec, handler);
    }

    void reset()
    {
        timer.disconnect();
    }
};

wf::touch_interface_t::touch_interface_t(wlr_cursor *cursor, wlr_seat *seat,
    input_surface_selector_t surface_at)
{
    this->cursor = cursor;
    this->seat   = seat;
    this->surface_at = surface_at;

    // connect handlers
    on_down.set_callback([=] (void *data)
    {
        auto ev   = static_cast<wlr_touch_down_event*>(data);
        auto mode = emit_device_event_signal(ev, &ev->touch->base);
        if (mode != input_event_processing_mode_t::IGNORE)
        {
            double lx, ly;
            wlr_cursor_absolute_to_layout_coords(cursor, &ev->touch->base, ev->x, ev->y, &lx, &ly);

            wf::pointf_t point;
            wf::get_core().output_layout->get_output_coords_at({lx, ly}, point);
            handle_touch_down(ev->touch_id, ev->time_msec, point, mode);
        }

        wf::get_core().seat->notify_activity();
        emit_device_post_event_signal(ev, &ev->touch->base);
    });

    on_up.set_callback([=] (void *data)
    {
        auto ev   = static_cast<wlr_touch_up_event*>(data);
        auto mode = emit_device_event_signal(ev, &ev->touch->base);
        if (mode != input_event_processing_mode_t::IGNORE)
        {
            handle_touch_up(ev->touch_id, ev->time_msec, mode);
        }

        wf::get_core().seat->notify_activity();
        emit_device_post_event_signal(ev, &ev->touch->base);
    });

    on_motion.set_callback([=] (void *data)
    {
        auto ev   = static_cast<wlr_touch_motion_event*>(data);
        auto mode = emit_device_event_signal(ev, &ev->touch->base);

        if (mode != input_event_processing_mode_t::IGNORE)
        {
            double lx, ly;
            wlr_cursor_absolute_to_layout_coords(
                wf::get_core_impl().seat->priv->cursor->cursor, &ev->touch->base,
                ev->x, ev->y, &lx, &ly);

            wf::pointf_t point;
            wf::get_core().output_layout->get_output_coords_at({lx, ly}, point);
            handle_touch_motion(ev->touch_id, ev->time_msec, point, true, mode);
        }

        wf::get_core().seat->notify_activity();
        emit_device_post_event_signal(ev, &ev->touch->base);
    });

    on_cancel.set_callback([=] (void *data)
    {
        wlr_touch_cancel_event *ev = (wlr_touch_cancel_event*)data;
        wlr_touch_up_event sim;
        sim.time_msec = ev->time_msec;
        sim.touch_id  = ev->touch_id;
        sim.touch     = ev->touch;
        this->on_up.emit(&sim);
    });

    on_frame.set_callback([&] (void*)
    {
        wlr_seat_touch_notify_frame(wf::get_core().get_current_seat());
        wf::get_core().seat->notify_activity();
    });

    on_up.connect(&cursor->events.touch_up);
    on_down.connect(&cursor->events.touch_down);
    on_frame.connect(&cursor->events.touch_frame);
    on_motion.connect(&cursor->events.touch_motion);
    on_cancel.connect(&cursor->events.touch_cancel);

    on_root_node_updated = [=] (wf::scene::root_node_update_signal *data)
    {
        if (!(data->flags & wf::scene::update_flag::INPUT_STATE))
        {
            return;
        }

        for (auto& [finger_id, node] : this->focus)
        {
            if (node && !is_grabbed_node_alive(node))
            {
                set_touch_focus(nullptr, finger_id, get_current_time(), {0, 0});
            }
        }
    };

    wf::get_core().scene()->connect(&on_root_node_updated);
    add_default_gestures();
}

wf::touch_interface_t::~touch_interface_t()
{}

const wf::touch::gesture_state_t& wf::touch_interface_t::get_state() const
{
    return this->finger_state;
}

wf::scene::node_ptr wf::touch_interface_t::get_focus(int finger_id) const
{
    auto it = this->focus.find(finger_id);
    return (it == focus.end() ? nullptr : it->second);
}

void wf::touch_interface_t::add_touch_gesture(
    nonstd::observer_ptr<touch::gesture_t> gesture)
{
    gesture->set_timer(std::make_unique<touch_timer_adapter_t>());
    this->gestures.emplace_back(gesture);
}

void wf::touch_interface_t::rem_touch_gesture(
    nonstd::observer_ptr<touch::gesture_t> gesture)
{
    gestures.erase(std::remove(gestures.begin(), gestures.end(), gesture),
        gestures.end());
}

void wf::touch_interface_t::set_touch_focus(wf::scene::node_ptr node,
    int id, int64_t time, wf::pointf_t point)
{
    if (focus[id] == node)
    {
        return;
    }

    if (focus[id])
    {
        focus[id]->touch_interaction().handle_touch_up(time, id, point);
    }

    focus[id] = node;
    if (node)
    {
        auto local = get_node_local_coords(node.get(), point);
        node->touch_interaction().handle_touch_down(time, id, local);
    }

    wf::touch_focus_changed_signal ev;
    ev.finger_id = id;
    ev.new_focus = node;
    wf::get_core().emit(&ev);
}

void wf::touch_interface_t::transfer_grab(scene::node_ptr grab_node)
{
    auto new_focus = grab_node->wants_raw_input() ? grab_node : nullptr;
    for (auto& [id, focused_node] : this->focus)
    {
        if (focused_node && (focused_node != new_focus) && !focused_node->wants_raw_input())
        {
            const auto lift_off_position = finger_state.fingers[id].current;
            focused_node->touch_interaction().handle_touch_up(get_current_time(), id,
                {lift_off_position.x, lift_off_position.y});
        }

        focused_node = new_focus;
    }
}

void wf::touch_interface_t::update_gestures(const wf::touch::gesture_event_t& ev)
{
    for (auto& gesture : this->gestures)
    {
        if ((this->finger_state.fingers.size() == 1) &&
            (ev.type == touch::EVENT_TYPE_TOUCH_DOWN))
        {
            gesture->reset(ev.time);
        }

        gesture->update_state(ev);
    }
}

void wf::touch_interface_t::handle_touch_down(int32_t id, uint32_t time,
    wf::pointf_t point, input_event_processing_mode_t mode)
{
    auto& seat = wf::get_core_impl().seat;
    seat->priv->break_mod_bindings();

    if (id == 0)
    {
        wf::get_core().seat->focus_output(
            wf::get_core().output_layout->get_output_at(point.x, point.y));
    }

    // NB. We first update the focus, and then update the gesture,
    // except if the input is grabbed.
    //
    // This is necessary because wm-focus needs to know the touch focus at the
    // moment the tap happens
    wf::touch::gesture_event_t gesture_event = {
        .type   = wf::touch::EVENT_TYPE_TOUCH_DOWN,
        .time   = time,
        .finger = id,
        .pos    = {point.x, point.y}
    };
    finger_state.update(gesture_event);

    if (mode != input_event_processing_mode_t::FULL)
    {
        update_gestures(gesture_event);
        update_cursor_state();
        return;
    }

    auto focus = this->surface_at(point);
    set_touch_focus(focus, id, time, point);

    seat->priv->update_drag_icon();
    update_gestures(gesture_event);
    update_cursor_state();
}

void wf::touch_interface_t::handle_touch_motion(int32_t id, uint32_t time,
    wf::pointf_t point, bool is_real_event, input_event_processing_mode_t mode)
{
    // handle_touch_motion is called on both real motion events and when
    // touch focus should be updated.
    //
    // In case this is not a real event, we don't want to update gestures,
    // because focus change can happen even while some gestures are still
    // updating.
    if (is_real_event)
    {
        const wf::touch::gesture_event_t gesture_event = {
            .type   = wf::touch::EVENT_TYPE_MOTION,
            .time   = time,
            .finger = id,
            .pos    = {point.x, point.y}
        };
        update_gestures(gesture_event);
        finger_state.update(gesture_event);
    }

    if (focus[id])
    {
        auto local = get_node_local_coords(focus[id].get(), point);
        focus[id]->touch_interaction().handle_touch_motion(time, id, local);
    }

    auto& seat = wf::get_core_impl().seat;
    seat->priv->update_drag_icon();
}

void wf::touch_interface_t::handle_touch_up(int32_t id, uint32_t time,
    input_event_processing_mode_t mode)
{
    const auto lift_off_position = finger_state.fingers[id].current;

    const wf::touch::gesture_event_t gesture_event = {
        .type   = wf::touch::EVENT_TYPE_TOUCH_UP,
        .time   = time,
        .finger = id,
        .pos    = lift_off_position,
    };

    update_gestures(gesture_event);
    finger_state.update(gesture_event);

    update_cursor_state();
    set_touch_focus(nullptr, id, time, {lift_off_position.x, lift_off_position.y});
}

void wf::touch_interface_t::update_cursor_state()
{
    auto& seat = wf::get_core_impl().seat;
    /* just set the cursor mode, independent of how many fingers we have */
    seat->priv->cursor->set_touchscreen_mode(true);
}

// Swipe params
constexpr static int EDGE_SWIPE_THRESHOLD  = 10;
constexpr static double MIN_SWIPE_DISTANCE = 30;
constexpr static double MAX_SWIPE_DISTANCE = 450;
constexpr static double SWIPE_INCORRECT_DRAG_TOLERANCE = 150;

// Pinch params
constexpr static double PINCH_INCORRECT_DRAG_TOLERANCE = 200;
constexpr static double PINCH_THRESHOLD = 1.5;

// General
constexpr static double GESTURE_INITIAL_TOLERANCE = 40;
constexpr static uint32_t GESTURE_BASE_DURATION   = 400;

using namespace wf::touch;
/**
 * swipe and with multiple fingers and directions
 */
class multi_action_t : public gesture_action_t
{
  public:
    multi_action_t(bool pinch, double threshold)
    {
        this->pinch     = pinch;
        this->threshold = threshold;
    }

    bool pinch;
    double threshold;
    bool last_pinch_was_pinch_in = false;
    double move_tolerance = 1e9;

    uint32_t target_direction = 0;
    int32_t cnt_fingers = 0;

    action_status_t update_state(const gesture_state_t& state,
        const gesture_event_t& event) override
    {
        if (event.type == wf::touch::EVENT_TYPE_TIMEOUT)
        {
            return wf::touch::ACTION_STATUS_CANCELLED;
        }

        if (event.type == EVENT_TYPE_TOUCH_UP)
        {
            return ACTION_STATUS_CANCELLED;
        }

        if (event.type == EVENT_TYPE_TOUCH_DOWN)
        {
            cnt_fingers = state.fingers.size();
            for (auto& finger : state.fingers)
            {
                if (glm::length(finger.second.delta()) > GESTURE_INITIAL_TOLERANCE)
                {
                    return ACTION_STATUS_CANCELLED;
                }
            }

            return ACTION_STATUS_RUNNING;
        }

        if (this->pinch)
        {
            if (glm::length(state.get_center().delta()) >= move_tolerance)
            {
                return ACTION_STATUS_CANCELLED;
            }

            double pinch = state.get_pinch_scale();
            last_pinch_was_pinch_in = pinch <= 1.0;
            if ((pinch <= 1.0 / threshold) || (pinch >= threshold))
            {
                return ACTION_STATUS_COMPLETED;
            }

            return ACTION_STATUS_RUNNING;
        }

        // swipe case
        if ((glm::length(state.get_center().delta()) >= MIN_SWIPE_DISTANCE) &&
            (this->target_direction == 0))
        {
            this->target_direction = state.get_center().get_direction();
        }

        if (this->target_direction == 0)
        {
            return ACTION_STATUS_RUNNING;
        }

        for (auto& finger : state.fingers)
        {
            if (finger.second.get_incorrect_drag_distance(this->target_direction) > this->move_tolerance)
            {
                return ACTION_STATUS_CANCELLED;
            }
        }

        if (state.get_center().get_drag_distance(this->target_direction) >=
            threshold)
        {
            return ACTION_STATUS_COMPLETED;
        }

        return ACTION_STATUS_RUNNING;
    }

    void reset(uint32_t time) override
    {
        gesture_action_t::reset(time);
        target_direction = 0;
    }
};

static uint32_t find_swipe_edges(wf::touch::point_t point)
{
    auto output   = wf::get_core().seat->get_active_output();
    auto geometry = output->get_layout_geometry();

    uint32_t edge_directions = 0;
    if (point.x <= geometry.x + EDGE_SWIPE_THRESHOLD)
    {
        edge_directions |= wf::GESTURE_DIRECTION_RIGHT;
    }

    if (point.x >= geometry.x + geometry.width - EDGE_SWIPE_THRESHOLD)
    {
        edge_directions |= wf::GESTURE_DIRECTION_LEFT;
    }

    if (point.y <= geometry.y + EDGE_SWIPE_THRESHOLD)
    {
        edge_directions |= wf::GESTURE_DIRECTION_DOWN;
    }

    if (point.y >= geometry.y + geometry.height - EDGE_SWIPE_THRESHOLD)
    {
        edge_directions |= wf::GESTURE_DIRECTION_UP;
    }

    return edge_directions;
}

static uint32_t wf_touch_to_wf_dir(uint32_t touch_dir)
{
    uint32_t gesture_dir = 0;
    if (touch_dir & MOVE_DIRECTION_RIGHT)
    {
        gesture_dir |= wf::GESTURE_DIRECTION_RIGHT;
    }

    if (touch_dir & MOVE_DIRECTION_LEFT)
    {
        gesture_dir |= wf::GESTURE_DIRECTION_LEFT;
    }

    if (touch_dir & MOVE_DIRECTION_UP)
    {
        gesture_dir |= wf::GESTURE_DIRECTION_UP;
    }

    if (touch_dir & MOVE_DIRECTION_DOWN)
    {
        gesture_dir |= wf::GESTURE_DIRECTION_DOWN;
    }

    return gesture_dir;
}

void wf::touch_interface_t::add_default_gestures()
{
    wf::option_wrapper_t<double> sensitivity{"input/gesture_sensitivity"};

    // Swipe gesture needs slightly less distance because it is usually
    // with many fingers and it is harder to move all of them
    auto swipe = std::make_unique<multi_action_t>(false,
        0.75 * MAX_SWIPE_DISTANCE / sensitivity);
    swipe->set_duration(GESTURE_BASE_DURATION * sensitivity);
    swipe->move_tolerance = SWIPE_INCORRECT_DRAG_TOLERANCE * sensitivity;

    const double pinch_thresh = 1.0 + (PINCH_THRESHOLD - 1.0) / sensitivity;
    auto pinch = std::make_unique<multi_action_t>(true, pinch_thresh);
    pinch->set_duration(GESTURE_BASE_DURATION * 1.5 * sensitivity);
    pinch->move_tolerance = PINCH_INCORRECT_DRAG_TOLERANCE * sensitivity;

    // Edge swipe needs a quick release to be considered edge swipe
    auto edge_swipe = std::make_unique<multi_action_t>(false,
        MAX_SWIPE_DISTANCE / sensitivity);
    auto edge_release = std::make_unique<wf::touch::touch_action_t>(1, false);
    edge_swipe->set_duration(GESTURE_BASE_DURATION * sensitivity);
    edge_swipe->move_tolerance = SWIPE_INCORRECT_DRAG_TOLERANCE * sensitivity;
    // The release action needs longer duration to handle the case where the
    // gesture is actually longer than the max distance.
    edge_release->set_duration(GESTURE_BASE_DURATION * 1.5 * sensitivity);

    nonstd::observer_ptr<multi_action_t> swp_ptr = swipe;
    nonstd::observer_ptr<multi_action_t> pnc_ptr = pinch;
    nonstd::observer_ptr<multi_action_t> esw_ptr = edge_swipe;

    std::vector<std::unique_ptr<gesture_action_t>> swipe_actions,
        edge_swipe_actions, pinch_actions;
    swipe_actions.emplace_back(std::move(swipe));
    pinch_actions.emplace_back(std::move(pinch));
    edge_swipe_actions.emplace_back(std::move(edge_swipe));
    edge_swipe_actions.emplace_back(std::move(edge_release));

    auto ack_swipe = [swp_ptr, this] ()
    {
        uint32_t possible_edges =
            find_swipe_edges(finger_state.get_center().origin);
        if (possible_edges)
        {
            return;
        }

        uint32_t direction = wf_touch_to_wf_dir(swp_ptr->target_direction);
        wf::touchgesture_t gesture{
            GESTURE_TYPE_SWIPE,
            direction,
            swp_ptr->cnt_fingers
        };
        wf::get_core().bindings->handle_gesture(gesture);
    };

    auto ack_edge_swipe = [esw_ptr, this] ()
    {
        uint32_t possible_edges = find_swipe_edges(finger_state.get_center().origin);
        uint32_t direction = wf_touch_to_wf_dir(esw_ptr->target_direction);

        possible_edges &= direction;
        if (possible_edges)
        {
            wf::touchgesture_t gesture{
                GESTURE_TYPE_EDGE_SWIPE,
                direction,
                esw_ptr->cnt_fingers
            };

            wf::get_core().bindings->handle_gesture(gesture);
        }
    };

    auto ack_pinch = [pnc_ptr] ()
    {
        wf::touchgesture_t gesture{GESTURE_TYPE_PINCH,
            pnc_ptr->last_pinch_was_pinch_in ? GESTURE_DIRECTION_IN :
            GESTURE_DIRECTION_OUT,
            pnc_ptr->cnt_fingers
        };

        wf::get_core().bindings->handle_gesture(gesture);
    };

    this->multiswipe = std::make_unique<gesture_t>(std::move(
        swipe_actions), ack_swipe);
    this->edgeswipe = std::make_unique<gesture_t>(std::move(
        edge_swipe_actions), ack_edge_swipe);
    this->multipinch = std::make_unique<gesture_t>(std::move(
        pinch_actions), ack_pinch);
    this->add_touch_gesture(this->multiswipe);
    this->add_touch_gesture(this->edgeswipe);
    this->add_touch_gesture(this->multipinch);
}
