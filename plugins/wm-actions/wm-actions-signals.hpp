#pragma once

#include <wayfire/signal-definitions.hpp>

namespace wf
{
/**
 * name: wm-actions-set-above-state
 * on: output
 * when: Emitted whenever some entity requests that the view's above state
 *   is supposed to change.
 * arguments: above: whether or not to set above state
 */
struct wm_actions_set_above_state : public wf::_view_signal
{
    /** The requested above state. If this is true, the view will be
     * added to the always-above layer. If it is false, the view will
     * be placed in the 'normal' workspace layer. */
    bool above;
};

/**
 * name: wm-actions-above-changed
 * on: output
 * when: Emitted whenever a views above layer has been changed.
 */
using wm_actions_above_changed = wf::_view_signal;
}
