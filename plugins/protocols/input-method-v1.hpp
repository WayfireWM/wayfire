#pragma once
#include <wayfire/signal-provider.hpp>

namespace wf
{
/**
 * on: core
 * Emitted when a text input (v1 or v3) is activated.
 */
struct input_method_v1_activate_signal
{};

/**
 * on: core
 * Emitted when a text input (v1 or v3) is deactivated.
 */
struct input_method_v1_deactivate_signal
{};
}
