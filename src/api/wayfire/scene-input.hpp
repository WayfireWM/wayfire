#pragma once

#include <wayfire/nonstd/wlroots.hpp>
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
     * Notify a node that it no longer has pointer focus.
     * This event is always sent after a corresponding pointer_enter event.
     */
    virtual void handle_pointer_leave()
    {}

    /**
     * Handle a button press or release event.
     *
     * When a node consumes a button event, core starts an *implicit grab* for it. This has the effect that
     * all subsequent input events are forwarded to that node, until all buttons are released. Thus, a node is
     * guaranteed to always receive matching press and release events, except when it explicitly opts out via
     * the RAW_INPUT node flag.
     *
     * @param pointer_position The position where the pointer is currently at.
     * @param button The wlr event describing the event.
     */
    virtual void handle_pointer_button(
        const wlr_pointer_button_event& event)
    {}

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
     * The user scrolled.
     *
     * @param pointer_position The position where the pointer is currently at.
     * @param event A structure describing the event.
     */
    virtual void handle_pointer_axis(const wlr_pointer_axis_event& event)
    {}
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
     * The user moved their finger without lifting it off.
     */
    virtual void handle_touch_motion(uint32_t time_ms, int finger_id,
        wf::pointf_t position)
    {}
};
}
