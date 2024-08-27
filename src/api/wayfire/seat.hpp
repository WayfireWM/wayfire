#pragma once

#include <memory>
#include <wayfire/scene.hpp>
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/view.hpp>

namespace wf
{
enum class keyboard_focus_reason
{
    USER,
    GRAB,
    REFOCUS,
    UNKNOWN,
};

/**
 * on: core
 * when: Keyboard focus is changed (may change to nullptr).
 */
struct keyboard_focus_changed_signal
{
    wf::scene::node_ptr new_focus;
    keyboard_focus_reason reason = keyboard_focus_reason::UNKNOWN;
};

/**
 * on: core
 * when: Seat activity has happened after being idle.
 */
struct seat_activity_signal
{};

/**
 * A seat represents a group of input devices (mouse, keyboard, etc.) which logically belong together.
 * Each seat has its own keyboard, touch, pointer and tablet focus.
 * Currently, Wayfire supports only a single seat.
 */
class seat_t
{
  public:
    // A pointer to the wlroots seat wlr_seat *const seat;
    wlr_seat*const seat;

    /**
     * Get the xkb_state of the currently active keyboard.
     * Note: may be null if there is no keyboard connected to the seat.
     */
    xkb_state *get_xkb_state();

    /**
     * Get a list of all currently pressed keys.
     */
    std::vector<uint32_t> get_pressed_keys();

    /**
     * Get a bitmask of the pressed modifiers on the current keyboard.
     * The returned value is a bitmask of WLR_MODIFIER_*.
     */
    uint32_t get_keyboard_modifiers();

    /**
     * Figure out whether the given keycode is a modifier on the current keyboard's keymap.
     * If yes, return the modifier as a WLR_MODIFIER_* bitmask, otherwise, return 0.
     */
    uint32_t modifier_from_keycode(uint32_t keycode);

    /**
     * Try to focus the given scenegraph node. This may not work if another node requests a higher focus
     * importance.
     *
     * Note that the focus_view function should be used for view nodes, as focusing views typically involves
     * more operations. Calling this function does not change the active view, even if the newly focused node
     * is a view node!
     *
     * The new_focus' last focus timestamp will be updated.
     */
    void set_active_node(wf::scene::node_ptr node,
        wf::keyboard_focus_reason reason = keyboard_focus_reason::UNKNOWN);

    /**
     * Get the node which has current keyboard focus.
     */
    wf::scene::node_ptr get_active_node();

    /**
     * Try to focus the given view. This may not work if another view or a node requests a higher focus
     * importance.
     */
    void focus_view(wayfire_view v);

    /**
     * Get the view which is currently marked as active. In general, this is the last view for which
     * @focus_view() was called, or for ex. when refocusing after a view disappears, the next view which
     * received focus.
     *
     * Usually, the active view has keyboard focus as well. In some cases (for ex. grabs), however, another
     * node might have the actual keyboard focus.
     */
    wayfire_view get_active_view() const;

    /**
     * Get the last focus timestamp which was given out by a set_active_node request.
     */
    uint64_t get_last_focus_timestamp() const;

    /**
     * Trigger a refocus operation.
     * See scene::node_t::keyboard_refocus() for details.
     */
    void refocus();

    /**
     * Focus the given output. The currently focused output is used to determine
     * which plugins receive various events (including bindings)
     */
    void focus_output(wf::output_t *o);

    /**
     * Get the currently focused "active" output
     */
    wf::output_t *get_active_output();

    /**
     * Notify clients and plugins of input activity on the seat
     */
    void notify_activity();

    /**
     * Create and initialize a new seat.
     */
    seat_t(wl_display *display, std::string name);
    ~seat_t();

    struct impl;
    std::unique_ptr<impl> priv;
};
}
