#include <cmath>

#include <wayfire/util/log.hpp>

#include "touch.hpp"
#include "cursor.hpp"
#include "input-manager.hpp"
#include "../core-impl.hpp"
#include "wayfire/output.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/compositor-surface.hpp"
#include "wayfire/output-layout.hpp"

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
        auto mode = emit_device_event_signal("touch_down", ev);

        double lx, ly;
        wlr_cursor_absolute_to_layout_coords(cursor, &ev->touch->base,
            ev->x, ev->y, &lx, &ly);

        wf::pointf_t point;
        wf::get_core().output_layout->get_output_coords_at({lx, ly}, point);
        handle_touch_down(ev->touch_id, ev->time_msec, point, mode);
        wlr_idle_notify_activity(wf::get_core().protocols.idle,
            wf::get_core().get_current_seat());
        emit_device_event_signal("touch_down_post", ev);
    });

    on_up.set_callback([=] (void *data)
    {
        auto ev   = static_cast<wlr_touch_up_event*>(data);
        auto mode = emit_device_event_signal("touch_up", ev);
        handle_touch_up(ev->touch_id, ev->time_msec, mode);
        wlr_idle_notify_activity(wf::get_core().protocols.idle,
            wf::get_core().get_current_seat());
        emit_device_event_signal("touch_up_post", ev);
    });

    on_motion.set_callback([=] (void *data)
    {
        auto ev   = static_cast<wlr_touch_motion_event*>(data);
        auto mode = emit_device_event_signal("touch_motion", ev);

        double lx, ly;
        wlr_cursor_absolute_to_layout_coords(
            wf::get_core_impl().seat->cursor->cursor, &ev->touch->base,
            ev->x, ev->y, &lx, &ly);

        wf::pointf_t point;
        wf::get_core().output_layout->get_output_coords_at({lx, ly}, point);
        handle_touch_motion(ev->touch_id, ev->time_msec, point, true, mode);
        wlr_idle_notify_activity(wf::get_core().protocols.idle,
            wf::get_core().get_current_seat());
        emit_device_event_signal("touch_motion_post", ev);
    });

    on_up.connect(&cursor->events.touch_up);
    on_down.connect(&cursor->events.touch_down);
    on_motion.connect(&cursor->events.touch_motion);

    on_surface_map_state_change.set_callback(
        [=] (wf::surface_interface_t *surface)
    {
        if ((this->grabbed_surface == surface) && !surface->is_mapped())
        {
            end_touch_down_grab();
            on_stack_order_changed.emit(nullptr);
        }
    });

    on_stack_order_changed.set_callback([=] (wf::signal_data_t *data)
    {
        for (auto f : this->get_state().fingers)
        {
            this->handle_touch_motion(f.first, get_current_time(),
                {f.second.current.x, f.second.current.y}, false,
                input_event_processing_mode_t::FULL);
        }
    });

    wf::get_core().connect_signal("output-stack-order-changed",
        &on_stack_order_changed);
    wf::get_core().connect_signal("view-geometry-changed", &on_stack_order_changed);

    add_default_gestures();
}

wf::touch_interface_t::~touch_interface_t()
{}

const wf::touch::gesture_state_t& wf::touch_interface_t::get_state() const
{
    return this->finger_state;
}

wf::surface_interface_t*wf::touch_interface_t::get_focus() const
{
    return this->focus;
}

void wf::touch_interface_t::set_grab(wf::plugin_grab_interface_t *grab)
{
    if (grab)
    {
        this->grab = grab;
        end_touch_down_grab();
        for (auto& f : this->get_state().fingers)
        {
            set_touch_focus(nullptr, f.first, get_current_time(), {0, 0});
        }
    } else
    {
        this->grab = nullptr;
        for (auto& f : this->get_state().fingers)
        {
            handle_touch_motion(f.first, get_current_time(),
                {f.second.current.x, f.second.current.y}, false,
                input_event_processing_mode_t::FULL);
        }
    }
}

void wf::touch_interface_t::add_touch_gesture(
    nonstd::observer_ptr<touch::gesture_t> gesture)
{
    this->gestures.emplace_back(gesture);
}

void wf::touch_interface_t::rem_touch_gesture(
    nonstd::observer_ptr<touch::gesture_t> gesture)
{
    gestures.erase(std::remove(gestures.begin(), gestures.end(), gesture),
        gestures.end());
}

void wf::touch_interface_t::set_touch_focus(wf::surface_interface_t *surface,
    int id, uint32_t time, wf::pointf_t point)
{
    bool focus_compositor_surface = wf::compositor_surface_from_surface(surface);
    bool had_focus = wlr_seat_touch_get_point(seat, id);
    wf::get_core_impl().seat->ensure_input_surface(surface);

    wlr_surface *next_focus = NULL;
    if (surface && !focus_compositor_surface)
    {
        next_focus = surface->get_wlr_surface();
    }

    // create a new touch point, we have a valid new focus
    if (!had_focus && next_focus)
    {
        wlr_seat_touch_notify_down(seat, next_focus, time, id, point.x, point.y);
    }

    if (had_focus && !next_focus)
    {
        wlr_seat_touch_notify_up(seat, time, id);
    }

    if (next_focus)
    {
        wlr_seat_touch_point_focus(seat, next_focus, time, id, point.x, point.y);
    }

    /* Manage the touch_focus, we take only the first finger for that */
    if (id == 0)
    {
        // first change the focus, so that plugins can freely grab input in
        // response to touch_down/up
        auto old_focus = this->focus;
        this->focus = surface;

        auto compositor_surface = compositor_surface_from_surface(old_focus);
        if (compositor_surface)
        {
            compositor_surface->on_touch_up();
        }

        compositor_surface = compositor_surface_from_surface(surface);
        if (compositor_surface)
        {
            compositor_surface->on_touch_down(point.x, point.y);
        }
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
    seat->break_mod_bindings();

    if (id == 0)
    {
        wf::get_core().focus_output(
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

    if (this->grab || (mode != input_event_processing_mode_t::FULL))
    {
        update_gestures(gesture_event);
        update_cursor_state();
        if (grab->callbacks.touch.down)
        {
            auto wo = wf::get_core().get_active_output();
            auto og = wo->get_layout_geometry();
            grab->callbacks.touch.down(id, point.x - og.x, point.y - og.y);
        }

        return;
    }

    wf::pointf_t local;
    auto focus = this->surface_at(point, local);
    if (finger_state.fingers.empty()) // finger state is not updated yet
    {
        start_touch_down_grab(focus);
    } else if (grabbed_surface && !seat->drag_active)
    {
        focus = grabbed_surface;
        local = get_surface_relative_coords(focus, point);
    }

    set_touch_focus(focus, id, time, local);

    seat->update_drag_icon();
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

    if (this->grab)
    {
        auto wo = wf::get_core().output_layout->get_output_at(point.x, point.y);
        auto og = wo->get_layout_geometry();
        if (grab->callbacks.touch.motion && is_real_event)
        {
            grab->callbacks.touch.motion(id, point.x - og.x, point.y - og.y);
        }

        return;
    }

    wf::pointf_t local;
    wf::surface_interface_t *surface = nullptr;
    auto& seat = wf::get_core_impl().seat;
    /* Same as cursor motion handling: make sure we send to the grabbed surface,
     * except if we need this for DnD */
    if (grabbed_surface && !seat->drag_icon)
    {
        surface = grabbed_surface;
        local   = get_surface_relative_coords(surface, point);
    } else
    {
        surface = surface_at(point, local);
        set_touch_focus(surface, id, time, local);
    }

    wlr_seat_touch_notify_motion(seat->seat, time, id, local.x, local.y);
    seat->update_drag_icon();

    auto compositor_surface = wf::compositor_surface_from_surface(surface);
    if ((id == 0) && compositor_surface && is_real_event)
    {
        compositor_surface->on_touch_motion(local.x, local.y);
    }
}

void wf::touch_interface_t::handle_touch_up(int32_t id, uint32_t time,
    input_event_processing_mode_t mode)
{
    const wf::touch::gesture_event_t gesture_event = {
        .type   = wf::touch::EVENT_TYPE_TOUCH_UP,
        .time   = time,
        .finger = id,
        .pos    = finger_state.fingers[id].current
    };
    update_gestures(gesture_event);
    finger_state.update(gesture_event);

    update_cursor_state();

    if (this->grab)
    {
        if (grab->callbacks.touch.up)
        {
            grab->callbacks.touch.up(id);
        }

        return;
    }

    set_touch_focus(nullptr, id, time, {0, 0});
    if (finger_state.fingers.empty())
    {
        end_touch_down_grab();
    }
}

void wf::touch_interface_t::start_touch_down_grab(
    wf::surface_interface_t *surface)
{
    this->grabbed_surface = surface;
}

void wf::touch_interface_t::end_touch_down_grab()
{
    if (grabbed_surface)
    {
        grabbed_surface = nullptr;
        for (auto& f : finger_state.fingers)
        {
            handle_touch_motion(f.first, wf::get_current_time(),
                {f.second.current.x, f.second.current.y}, false,
                input_event_processing_mode_t::FULL);
        }
    }
}

void wf::touch_interface_t::update_cursor_state()
{
    auto& seat = wf::get_core_impl().seat;
    /* just set the cursor mode, independent of how many fingers we have */
    seat->cursor->set_touchscreen_mode(true);
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

    uint32_t target_direction = 0;
    int32_t cnt_fingers = 0;

    action_status_t update_state(const gesture_state_t& state,
        const gesture_event_t& event) override
    {
        if (event.time - this->start_time > this->get_duration())
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
            if (glm::length(state.get_center().delta()) >= get_move_tolerance())
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
            if (finger.second.get_incorrect_drag_distance(this->target_direction) >
                this->get_move_tolerance())
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
    auto output   = wf::get_core().get_active_output();
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
    swipe->set_move_tolerance(SWIPE_INCORRECT_DRAG_TOLERANCE * sensitivity);

    const double pinch_thresh = 1.0 + (PINCH_THRESHOLD - 1.0) / sensitivity;
    auto pinch = std::make_unique<multi_action_t>(true, pinch_thresh);
    pinch->set_duration(GESTURE_BASE_DURATION * 1.5 * sensitivity);
    pinch->set_move_tolerance(PINCH_INCORRECT_DRAG_TOLERANCE * sensitivity);

    // Edge swipe needs a quick release to be considered edge swipe
    auto edge_swipe = std::make_unique<multi_action_t>(false,
        MAX_SWIPE_DISTANCE / sensitivity);
    auto edge_release = std::make_unique<wf::touch::touch_action_t>(1, false);
    edge_swipe->set_duration(GESTURE_BASE_DURATION * sensitivity);
    edge_swipe->set_move_tolerance(SWIPE_INCORRECT_DRAG_TOLERANCE * sensitivity);
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
        wf::get_core_impl().input->get_active_bindings().handle_gesture(gesture);
    };

    auto ack_edge_swipe = [esw_ptr, this] ()
    {
        uint32_t possible_edges =
            find_swipe_edges(finger_state.get_center().origin);
        uint32_t direction = wf_touch_to_wf_dir(esw_ptr->target_direction);

        possible_edges &= direction;
        if (possible_edges)
        {
            wf::touchgesture_t gesture{
                GESTURE_TYPE_EDGE_SWIPE,
                direction,
                esw_ptr->cnt_fingers
            };

            wf::get_core_impl().input->get_active_bindings().handle_gesture(gesture);
        }
    };

    auto ack_pinch = [pnc_ptr] ()
    {
        wf::touchgesture_t gesture{GESTURE_TYPE_PINCH,
            pnc_ptr->last_pinch_was_pinch_in ? GESTURE_DIRECTION_IN :
            GESTURE_DIRECTION_OUT,
            pnc_ptr->cnt_fingers
        };

        wf::get_core_impl().input->get_active_bindings().handle_gesture(gesture);
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
