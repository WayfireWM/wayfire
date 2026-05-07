#pragma once

#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/geometry.hpp>
#include <wayfire/region.hpp>
#include <optional>
#include <memory>

namespace wf
{
class seat_t;
namespace scene
{
class node_t;
using node_ptr = std::shared_ptr<node_t>;
}

/**
 * Describes why input is delivered to a node even when another node may be
 * under the pointer/touch position.
 */
enum class input_grab_kind_t
{
    /**
     * No grab is active. Events are delivered to the node under the pointer/touch position.
     */
    NONE,
    /**
     * An implicit grab started by core after a node consumed a press/down event. Further events are normally
     * kept on the same node until the interaction ends, but the grab may be retargeted if
     * can_retarget_*_grab() allows it.
     */
    IMPLICIT,
    /**
     * Input delivery while a drag-and-drop operation is active. The grab started when the drag-and-drop
     * operation started, and typically ends when the user releases the button or lifts the finger, or when
     * the drag-and-drop operation is cancelled.
     */
    DND,
    /**
     * A node or protocol explicitly requested exclusive delivery of the ongoing interaction. The grab started
     * when the node/protocol requested it, and typically ends when the user releases the button or lifts the
     * finger, or when the grab is cancelled by the node/protocol.
     */
    EXPLICIT,
};

/**
 * When refocusing on a particular output, there may be multiple nodes
 * which can receive keyboard focus. While usually the most recently focused
 * node is chosen, there are cases where this is not the desired behavior, for ex.
 * nodes which have keyboard grabs. In order to accommodate for these cases,
 * the focus_importance enum provides a way for nodes to indicate in what cases
 * they should receive keyboard focus.
 */
enum class focus_importance
{
    // No focus at all.
    NONE    = 0,
    // Node may accept focus, but further nodes should override it if sensible.
    LOW     = 1,
    // Regularly focused node (typically regular views).
    REGULAR = 2,
    // Highest priority. Nodes which request focus like this usually do not
    // get their requests overridden.
    HIGH    = 3,
};

struct keyboard_focus_node_t
{
    scene::node_t *node = nullptr;
    focus_importance importance = focus_importance::NONE;

    // Whether nodes below this node are allowed to get focus, no matter their
    // focus importance.
    bool allow_focus_below = true;

    /**
     * True iff:
     * 1. The other node has a higher focus importance
     * 2. Or, the other node has the same importance but a newer
     *   last_focus_timestamp.
     */
    bool operator <(const keyboard_focus_node_t& other) const;
};

/**
 * An interface for scene nodes which interact with the keyboard.
 *
 * Note that by default, nodes do not receive keyboard input. Nodes which wish
 * to do so need to have node_flags::ACTIVE_KEYBOARD set.
 */
class keyboard_interaction_t
{
  public:
    /**
     * Handle a keyboard enter event.
     * This means that the node is now focused.
     */
    virtual void handle_keyboard_enter(wf::seat_t *seat)
    {}

    /**
     * Handle a keyboard leave event.
     * The node is no longer focused.
     */
    virtual void handle_keyboard_leave(wf::seat_t *seat)
    {}

    /**
     * Handle a keyboard key event.
     *
     * These are received only after the node has received keyboard focus and
     * before it loses it.
     *
     * @return What should happen with the further processing of the event.
     */
    virtual void handle_keyboard_key(wf::seat_t *seat, wlr_keyboard_key_event event)
    {}

    keyboard_interaction_t() = default;
    virtual ~keyboard_interaction_t()
    {}

    /**
     * The last time(nanoseconds since epoch) when the node was focused.
     * Updated automatically by core.
     */
    uint64_t last_focus_timestamp = 0;
};

/**
 * An interface for scene nodes which interact with pointer input.
 *
 * As opposed to keyboard input, all nodes are eligible for receiving pointer
 * and input. As a result, every node may receive motion, button, etc. events.
 * Nodes which do not wish to process events may simply not accept input at
 * any point (as the default accepts_input implementation does).
 */
class pointer_interaction_t
{
  protected:
    pointer_interaction_t(const pointer_interaction_t&) = delete;
    pointer_interaction_t(pointer_interaction_t&&) = delete;
    pointer_interaction_t& operator =(const pointer_interaction_t&) = delete;
    pointer_interaction_t& operator =(pointer_interaction_t&&) = delete;

  public:
    pointer_interaction_t() = default;
    virtual ~pointer_interaction_t() = default;

    /**
     * The pointer entered the node and thus the node gains pointer focus.
     */
    virtual void handle_pointer_enter(wf::pointf_t position)
    {}

    /**
     * The pointer entered the node and thus the node gains pointer focus.
     *
     * @param grab Describes whether this enter is part of normal focus updates
     *   or the result of an ongoing grab being delivered to this node.
     */
    virtual void handle_pointer_enter(wf::pointf_t position,
        input_grab_kind_t grab)
    {
        handle_pointer_enter(position);
    }

    /**
     * Notify a node that it no longer has pointer focus.
     * This event is always sent after a corresponding pointer_enter event.
     */
    virtual void handle_pointer_leave()
    {}

    /**
     * Notify a node that it no longer has pointer focus.
     * This event is always sent after a corresponding pointer_enter event.
     *
     * @param grab Describes whether this leave is part of normal focus updates
     *   or the result of an ongoing grab moving away from this node.
     */
    virtual void handle_pointer_leave(input_grab_kind_t grab)
    {
        handle_pointer_leave();
    }

    /**
     * Handle a button press or release event.
     *
     * When a node consumes a button event, core starts an *implicit grab* for it. This has the effect that
     * all subsequent input events are forwarded to that node, until all buttons are released. Thus, a node is
     * guaranteed to always receive matching press and release events, except when it explicitly opts out via
     * the RAW_INPUT node flag.
     *
     * @param button The wlr event describing the event.
     */
    virtual void handle_pointer_button(const wlr_pointer_button_event& event)
    {}

    /**
     * Handle a button press or release event.
     *
     * When a node consumes a button event, core starts an *implicit grab* for it. This has the effect that
     * all subsequent input events are forwarded to that node, until all buttons are released. Thus, a node is
     * guaranteed to always receive matching press and release events, except when it explicitly opts out via
     * the RAW_INPUT node flag.
     *
     * @param button The wlr event describing the event.
     * @param grab Describes whether the event is delivered normally or as part
     *   of an ongoing grab. For the initial press this is typically NONE; for
     *   later events it may be IMPLICIT, DND or EXPLICIT.
     */
    virtual void handle_pointer_button(
        const wlr_pointer_button_event& event,
        input_grab_kind_t grab)
    {
        handle_pointer_button(event);
    }

    /**
     * The user moved the pointer.
     *
     * @param pointer_position The new position of the pointer.
     * @param time_ms The time reported by the device when the event happened.
     */
    virtual void handle_pointer_motion(wf::pointf_t pointer_position,
        uint32_t time_ms)
    {}

    /**
     * The user moved the pointer.
     *
     * @param pointer_position The new position of the pointer.
     * @param time_ms The time reported by the device when the event happened.
     * @param grab Describes whether the motion follows the hovered node
     *   normally or is being redirected by an active grab.
     */
    virtual void handle_pointer_motion(wf::pointf_t pointer_position,
        uint32_t time_ms, input_grab_kind_t grab)
    {
        handle_pointer_motion(pointer_position, time_ms);
    }

    /**
     * The user scrolled.
     *
     * @param pointer_position The position where the pointer is currently at.
     * @param event A structure describing the event.
     */
    virtual void handle_pointer_axis(const wlr_pointer_axis_event& event)
    {}

    /**
     * Check whether an ongoing pointer grab may be retargeted to another node.
     * The default behavior is conservative and denies retargeting.
     *
     * @param kind The type of active grab whose focus is about to move.
     * @param new_target The candidate node which would receive future events.
     * @param global_position The current pointer position in global layout
     *   coordinates.
     */
    virtual bool can_retarget_pointer_grab(input_grab_kind_t kind,
        nonstd::observer_ptr<scene::node_t> new_target, wf::pointf_t global_position)
    {
        return false;
    }
};

/**
 * An interface for scene nodes which interact with touch input.
 */
class touch_interaction_t
{
  public:
    touch_interaction_t() = default;
    virtual ~touch_interaction_t() = default;

    /**
     * The user pressed down with a finger on the node.
     *
     * @param finger_id The id of the finger pressed down (first is 0, then 1,
     *   2, ..). Note that it is possible that the finger 0 is pressed down on
     *   another node, then the current node may start receiving touch down
     *   events beginning with finger 1, 2, ...
     *
     * @param position The coordinates of the finger.
     */
    virtual void handle_touch_down(uint32_t time_ms, int finger_id,
        wf::pointf_t position)
    {}

    /**
     * The user pressed down with a finger on the node.
     *
     * @param finger_id The id of the finger pressed down (first is 0, then 1,
     *   2, ..). Note that it is possible that the finger 0 is pressed down on
     *   another node, then the current node may start receiving touch down
     *   events beginning with finger 1, 2, ...
     *
     * @param position The coordinates of the finger.
     * @param grab Describes whether the event is delivered normally or as part
     *   of an ongoing touch grab.
     */
    virtual void handle_touch_down(uint32_t time_ms, int finger_id,
        wf::pointf_t position, input_grab_kind_t grab)
    {
        handle_touch_down(time_ms, finger_id, position);
    }

    /**
     * The user lifted their finger off the node.
     *
     * @param finger_id The id of the finger being lifted. It is guaranteed that
     *   the finger will have been pressed on the node before.
     * @param lift_off_position The last position the finger had before the
     *   lift off.
     */
    virtual void handle_touch_up(uint32_t time_ms, int finger_id,
        wf::pointf_t lift_off_position)
    {}

    /**
     * The user lifted their finger off the node.
     *
     * @param finger_id The id of the finger being lifted. It is guaranteed that
     *   the finger will have been pressed on the node before.
     * @param lift_off_position The last position the finger had before the
     *   lift off.
     * @param grab Describes whether the event is delivered normally or as part
     *   of an ongoing touch grab.
     */
    virtual void handle_touch_up(uint32_t time_ms, int finger_id,
        wf::pointf_t lift_off_position, input_grab_kind_t grab)
    {
        handle_touch_up(time_ms, finger_id, lift_off_position);
    }

    /**
     * The user moved their finger without lifting it off.
     */
    virtual void handle_touch_motion(uint32_t time_ms, int finger_id,
        wf::pointf_t position)
    {}

    /**
     * The user moved their finger without lifting it off.
     *
     * @param grab Describes whether the motion follows the touched node
     *   normally or is being redirected by an active grab.
     */
    virtual void handle_touch_motion(uint32_t time_ms, int finger_id,
        wf::pointf_t position, input_grab_kind_t grab)
    {
        handle_touch_motion(time_ms, finger_id, position);
    }

    /**
     * Check whether an ongoing touch grab may be retargeted to another node.
     * The default behavior is conservative and denies retargeting.
     *
     * @param kind The type of active grab whose focus is about to move.
     * @param new_target The candidate node which would receive future events.
     * @param finger_id The finger whose grab would be retargeted.
     * @param global_position The current touch position in global layout
     *   coordinates.
     */
    virtual bool can_retarget_touch_grab(input_grab_kind_t kind,
        nonstd::observer_ptr<scene::node_t> new_target, int finger_id,
        wf::pointf_t global_position)
    {
        return false;
    }
};
}
