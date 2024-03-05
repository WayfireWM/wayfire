#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <map>
#include <vector>
#include <chrono>

#include "seat-impl.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/view.hpp"
#include "wayfire/core.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/option-wrapper.hpp>

namespace wf
{
/**
 * input_manager is a class which manages high-level input state:
 * 1. Active grabs
 * 2. Exclusive clients
 * 3. Available input devices
 */
class input_manager_t
{
  private:
    wf::wl_listener_wrapper input_device_created;
    wf::wl_idle_call idle_update_cursor;

    wf::signal::connection_t<wf::reload_config_signal> config_updated;
    wf::signal::connection_t<output_added_signal> output_added;

  public:
    /**
     * Locked mods are stored globally because the keyboard devices might be
     * destroyed and created again by wlroots.
     */
    uint32_t locked_mods = 0;

    /**
     * Go through all input devices and map them to outputs as specified in the
     * config file or by hints in the wlroots backend.
     */
    void refresh_device_mappings();

    input_manager_t();
    ~input_manager_t() = default;

    /** Initialize a new input device */
    void handle_new_input(wlr_input_device *dev);
    /** Destroy an input device */
    void handle_input_destroyed(wlr_input_device *dev);

    wl_client *exclusive_client = NULL;
    /**
     * Set the exclusive client.
     * Only it can get pointer focus from now on.
     * Exceptions are allowed for special views like OSKs.
     */
    void set_exclusive_focus(wl_client *client);

    std::vector<std::unique_ptr<wf::input_device_impl_t>> input_devices;
};
}

/**
 * Emit a signal for device events.
 */
template<class EventType>
wf::input_event_processing_mode_t emit_device_event_signal(EventType *event, wlr_input_device *device)
{
    wf::input_event_signal<EventType> data;
    data.event  = event;
    data.device = device;
    wf::get_core().emit(&data);
    return data.mode;
}

template<class EventType>
void emit_device_post_event_signal(EventType *event, wlr_input_device *device)
{
    wf::post_input_event_signal<EventType> data;
    data.event  = event;
    data.device = device;
    wf::get_core().emit(&data);
}

#endif /* end of include guard: INPUT_MANAGER_HPP */
