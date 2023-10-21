#pragma once

#include "wayfire/geometry.hpp"
#include <memory>
#include <vector>
#include <wayfire/bindings.hpp>
#include <wayfire/config/option-wrapper.hpp>
#include <wayfire/config/types.hpp>

namespace wf
{
/**
 * bindings_repository_t is responsible for managing a list of all bindings in
 * Wayfire, and for calling these bindings on the corresponding events.
 */
class bindings_repository_t
{
  public:
    bindings_repository_t();
    ~bindings_repository_t();

    /**
     * Plugins can use the add_* functions to register their own bindings.
     *
     * @param key/axis/button/activator The corresponding values are used to know when to trigger this
     *   binding. The values that the shared pointer points to may be modified, in which case core will use
     *   the latest value stored there.
     *
     * @param cb The plugin callback to be executed when the binding is triggered. Can be used to unregister
     *   the binding.
     */
    void add_key(option_sptr_t<keybinding_t> key, wf::key_callback *cb);
    void add_axis(option_sptr_t<keybinding_t> axis, wf::axis_callback *cb);
    void add_button(option_sptr_t<buttonbinding_t> button, wf::button_callback *cb);
    void add_activator(option_sptr_t<activatorbinding_t> activator, wf::activator_callback *cb);

    /**
     * Handle a keybinding pressed by the user.
     *
     * @param pressed The keybinding which was triggered.
     * @param mod_binding_key The modifier which triggered the binding, if any.
     *
     * @return true if any of the matching registered bindings consume the event.
     */
    bool handle_key(const wf::keybinding_t& pressed, uint32_t mod_binding_key);

    /** Handle an axis event. */
    bool handle_axis(uint32_t modifiers, wlr_pointer_axis_event *ev);

    /**
     * Handle a buttonbinding pressed by the user.
     * @return true if any of the matching registered bindings consume the event.
     */
    bool handle_button(const wf::buttonbinding_t& pressed);

    /** Handle a gesture from the user. */
    void handle_gesture(const wf::touchgesture_t& gesture);

    /** Erase binding of any type by callback */
    void rem_binding(void *callback);

    /**
     * Enable or disable the repository. The state is reference-counted and starts at 1 (enabled).
     */
    void set_enabled(bool enabled);

    struct impl;
    std::unique_ptr<impl> priv;
};
}
