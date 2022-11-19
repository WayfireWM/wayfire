#ifndef WF_BINDINGS_HPP
#define WF_BINDINGS_HPP

#include <functional>
#include <cstdint>
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/config/types.hpp>

namespace wf
{
struct touchgesture_t;
struct binding_t;

/**
 * A callback for key bindings.
 * Receives as a parameter the key combination which activated it.
 *
 * The returned value indicates whether the key event has been consumed, in which
 * case it will not be sent to clients (but may still be received by other plugins).
 */
using key_callback = std::function<bool (const wf::keybinding_t&)>;

/**
 * A callback for button bindings.
 * Receives as a parameter the button combination which activated it.
 *
 * The returned value indicates whether the button event has been consumed, in
 * which case it will not be sent to clients (but may still be received by other
 * plugins).
 */
using button_callback = std::function<bool (const wf::buttonbinding_t&)>;

/**
 * A callback for axis bindings.
 * Receives as a parameter an axis event from wlroots.
 *
 * The returned value indicates whether the event has been consumed, in which
 * case it will not be sent to clients (but may still be received by other
 * plugins).
 */
using axis_callback = std::function<bool (wlr_pointer_axis_event*)>;

/**
 * Describes the possible event sources that can activate an activator binding.
 */
enum class activator_source_t
{
    /** Binding activated by a keybinding. */
    KEYBINDING,
    /** Binding activated by a modifier keybinding. */
    MODIFIERBINDING,
    /** Binding activated by a button binding. */
    BUTTONBINDING,
    /** Binding activated by a touchscreen gesture. */
    GESTURE,
    /** Binding activated by a hotspot. */
    HOTSPOT,
    /** Binding was activated by another plugin. */
    PLUGIN,
    /** Binding was activated by another plugin with custom data */
    PLUGIN_WITH_DATA,
};

/**
 * Data sent to activator bindings when they are activated.
 * Includes information from the activating event source.
 *
 * Note: some plugins might support extended activator data, i.e they might
 * accept a subclass of activator_data_t when source is PLUGIN_WITH_DATA.
 */
struct activator_data_t
{
    /** The activating source type */
    activator_source_t source;

    /**
     * Additional data from the event source which activates the activator.
     *
     * - The key which was pressed for KEYBINDING
     * - The modifier which was released for MODIFIERBINDING
     * - The button pressed for BUTTONBINDING
     * - The hotspot edges for HOTSPOT
     * - undefined otherwise
     */
    uint32_t activation_data;
};

using activator_callback = std::function<bool (const wf::activator_data_t&)>;
}

#endif /* end of include guard: WF_BINDINGS_HPP */
