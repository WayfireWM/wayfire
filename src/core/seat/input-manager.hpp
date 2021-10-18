#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <map>
#include <vector>
#include <chrono>

#include "seat.hpp"
#include "bindings-repository.hpp"
#include "wayfire/plugin.hpp"
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

    wf::signal_connection_t config_updated;
    wf::signal_connection_t output_added;

    input_manager_t(const input_manager_t&) = delete;
    input_manager_t(input_manager_t&&) = delete;
    input_manager_t& operator =(const input_manager_t&) = delete;
    input_manager_t& operator =(input_manager_t&&) = delete;

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

    wf::plugin_grab_interface_t *active_grab = nullptr;

    /** Set the currently active grab interface */
    bool grab_input(wf::plugin_grab_interface_t*);
    /** Unset the active grab interface */
    void ungrab_input();
    /** @return true if the input is grabbed */
    bool input_grabbed();

    wl_client *exclusive_client = NULL;
    /**
     * Set the exclusive client.
     * Only it can get pointer focus from now on.
     * Exceptions are allowed for special views like OSKs.
     */
    void set_exclusive_focus(wl_client *client);

    std::vector<std::unique_ptr<wf::input_device_impl_t>> input_devices;

    /**
     * Check if the given surface is focuseable at the moment.
     * This depends on things like exclusive clients, etc.
     */
    bool can_focus_surface(wf::focused_view_t surface);

    // returns the surface under the given global coordinates
    // if no such surface (return NULL), lx and ly are undefined
    wf::focused_view_t input_surface_at(wf::pointf_t global,
        wf::pointf_t& local);

    /** @return the bindings for the active output */
    wf::bindings_repository_t& get_active_bindings();
};
}

/**
 * Emit a signal for device events.
 */
template<class EventType>
wf::input_event_processing_mode_t emit_device_event_signal(
    std::string event_name, EventType *event)
{
    wf::input_event_signal<EventType> data;
    data.event = event;
    wf::get_core().emit_signal(event_name, &data);

    return data.mode;
}

#endif /* end of include guard: INPUT_MANAGER_HPP */
