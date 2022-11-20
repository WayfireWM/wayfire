#pragma once

#include "wayfire/geometry.hpp"
#include <memory>
#include <vector>
#include <wayfire/bindings.hpp>
#include <wayfire/config/option-wrapper.hpp>
#include <wayfire/config/types.hpp>
#include "hotspot-manager.hpp"

namespace wf
{
/**
 * bindings_repository_t is responsible for managing a list of all bindings in
 * Wayfire, and for calling these bindings on the corresponding events.
 */
class bindings_repository_t
{
  public:
    bindings_repository_t(wf::output_t *output);

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
     *
     * @return true if any of the matching registered bindings consume the event.
     */
    bool handle_button(const wf::buttonbinding_t& pressed);

    /** Handle a gesture from the user. */
    void handle_gesture(const wf::touchgesture_t& gesture);

    /** Handle a direct call to an activator binding */
    bool handle_activator(
        const std::string& activator, const wf::activator_data_t& data);

    /** Erase binding of any type by callback */
    void rem_binding(void *callback);
    /** Erase binding of any type */
    void rem_binding(binding_t *binding);

    /**
     * Recreate hotspots.
     *
     * The action will take place on the next idle.
     */
    void recreate_hotspots();

  private:
    // output_t directly pushes in the binding containers to avoid having the
    // same wrapped functions as in the output public API.
    friend class output_impl_t;

    binding_container_t<wf::keybinding_t, key_callback> keys;
    binding_container_t<wf::keybinding_t, axis_callback> axes;
    binding_container_t<wf::buttonbinding_t, button_callback> buttons;
    binding_container_t<wf::activatorbinding_t, activator_callback> activators;

    hotspot_manager_t hotspot_mgr;

    wf::signal_connection_t on_config_reload;
    wf::wl_idle_call idle_recreate_hotspots;
};
}
