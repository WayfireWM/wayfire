#pragma once

#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/region.hpp>
#include <optional>
#include <memory>

namespace wf
{
namespace scene
{
class node_t;
using node_ptr = std::shared_ptr<node_t>;
}

/**
 * When a node receives any type of input, it should report how it handled the
 * event. Thus, depending on the node, the input event may be propagated to other
 * nodes or not.
 */
enum class input_action
{
    /** Do not propagate events further, current node consumed the event. */
    CONSUME,
    /** Event processed by the node, but other nodes should also receive it. */
    PROPAGATE,
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
    keyboard_interaction_t(const keyboard_interaction_t&) = delete;
    keyboard_interaction_t(keyboard_interaction_t&&) = delete;
    keyboard_interaction_t& operator =(const keyboard_interaction_t&) = delete;
    keyboard_interaction_t& operator =(keyboard_interaction_t&&) = delete;

    /**
     * Handle a keyboard enter event.
     * This means that the node is now focused.
     */
    virtual void handle_keyboard_enter()
    {}

    /**
     * Handle a keyboard leave event.
     * The node is no longer focused.
     */
    virtual void handle_keyboard_leave()
    {}

    /**
     * Handle a keyboard key event.
     *
     * These are received only after the node has received keyboard focus and
     * before it loses it.
     *
     * @return What should happen with the further processing of the event.
     */
    virtual input_action handle_keyboard_key(wlr_event_keyboard_key event)
    {
        return input_action::PROPAGATE;
    }

    keyboard_interaction_t() = default;
    virtual ~keyboard_interaction_t()
    {}
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
     * Check whether the node wants to receive input events when the cursor is
     * at the given @pointer_position and how it processes them.
     *
     * Note that the view may receive events even if the pointer is outside of
     * the set of points where the node accepts input. This can happen for ex.
     * when an implicit grab is started.
     */
    virtual bool accepts_input(wf::pointf_t pointer_position)
    {
        return false;
    }

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
     * When a node consumes a button event, core starts an *implicit grab* for it.
     * This has the effect that all subsequent input events are forwarded to that
     * node, until all buttons are released. Thus, a node is guaranteed to always
     * receive matching press and release events.
     *
     * @param pointer_position The position where the pointer is currently at.
     * @param button The wlr event describing the event.
     */
    virtual input_action handle_pointer_button(
        const wlr_event_pointer_button& event)
    {
        return input_action::PROPAGATE;
    }

    /**
     * The user moved the pointer.
     *
     * @param pointer_position The new position of the pointer.
     * @param time_ms The time reported by the device when the event happened.
     */
    virtual input_action handle_pointer_motion(wf::pointf_t pointer_position,
        uint32_t time_ms)
    {
        return input_action::PROPAGATE;
    }

    /**
     * The user scrolled.
     *
     * @param pointer_position The position where the pointer is currently at.
     * @param event A structure describing the event.
     */
    virtual input_action handle_pointer_axis(const wlr_event_pointer_axis& event)
    {
        return input_action::PROPAGATE;
    }
};
}
