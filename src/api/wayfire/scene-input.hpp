#pragma once

#include <wayfire/nonstd/wlroots.hpp>

namespace wf
{
/**
 * After a node receives keyboard input, it can decide whether input should
 * be propagated to other nodes or not.
 */
enum class keyboard_action
{
    /** Do not propagate events further, current node consumed the event. */
    CONSUME,
    /** Propagate to other nodes which want the keyboard events. */
    PROPAGATE,
};

/**
 * An interface for scene nodes which interact with the keyboard.
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
    virtual keyboard_action handle_keyboard_key(wlr_event_keyboard_key event)
    {
        return keyboard_action::PROPAGATE;
    }

    keyboard_interaction_t() = default;
    virtual ~keyboard_interaction_t()
    {}
};
}
