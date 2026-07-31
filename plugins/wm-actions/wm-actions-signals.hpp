#pragma once

#include <wayfire/signal-definitions.hpp>

namespace wf
{
/**
 * on: output
 * when: Emitted whenever some entity requests that the view's above state
 *   is supposed to change.
 * arguments: above: whether or not to set above state
 */
struct wm_actions_set_above_state_signal
{
    wayfire_view view;

    /** The requested above state. If this is true, the view will be
     * added to the always-above layer. If it is false, the view will
     * be placed in the 'normal' workspace layer. */
    bool above;
};

/**
 * on: output
 * when: Emitted whenever some entity requests that the view's below state
 *   is supposed to change.
 * arguments: below: whether or not to set below state
 */
struct wm_actions_set_below_state_signal
{
    wayfire_view view;

    /** The requested below state. If this is true, the view will be
     * added to the always-below layer. If it is false, the view will
     * be placed in the 'normal' workspace layer. */
    bool below;
};

/**
 * on: output
 * when: Emitted whenever a views above layer has been changed.
 */
struct wm_actions_above_changed_signal
{
    wayfire_view view;
};

/**
 * on: output
 * when: Emitted whenever a views below layer has been changed.
 */
struct wm_actions_below_changed_signal
{
    wayfire_view view;
};
}
