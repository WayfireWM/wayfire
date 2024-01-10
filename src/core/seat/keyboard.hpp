#pragma once

#include <chrono>
#include "seat-impl.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/util.hpp"
#include <wayfire/option-wrapper.hpp>

namespace wf
{
enum locked_mods_t
{
    KB_MOD_NUM_LOCK  = 1 << 0,
    KB_MOD_CAPS_LOCK = 1 << 1,
};

/**
 * Represents a logical keyboard.
 */
class keyboard_t
{
  public:
    keyboard_t(wlr_input_device *keyboard);
    ~keyboard_t();

    wlr_keyboard *handle;
    wlr_input_device *device;

    /** Get the currently pressed modifiers */
    uint32_t get_modifiers();

    /* The keycode which triggered the modifier binding */
    uint32_t mod_binding_key = 0;

    /** Convert a key to a modifier */
    uint32_t mod_from_key(uint32_t key);

  private:
    wf::wl_listener_wrapper on_key, on_modifier;
    void setup_listeners();

    wf::signal::connection_t<wf::reload_config_signal> on_config_reload;
    void reload_input_options();

    wf::option_wrapper_t<std::string> model, variant, layout, options, rules;
    wf::option_wrapper_t<int> repeat_rate, repeat_delay;
    /** Options have changed in the config file */
    bool dirty_options = true;

    std::chrono::steady_clock::time_point mod_binding_start;

    bool handle_keyboard_key(uint32_t key, uint32_t state);

    /** Get the current locked mods */
    uint32_t get_locked_mods();

    /** Check whether we have only modifiers pressed down */
    bool has_only_modifiers();
};
}
