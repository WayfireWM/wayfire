#ifndef CORE_HPP
#define CORE_HPP

#include "wayfire/object.hpp"
#include "wayfire/scene-input.hpp"
#include <wayfire/geometry.hpp>
#include <wayfire/idle.hpp>
#include <wayfire/config-backend.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/signal-provider.hpp>

#include <sys/types.h>
#include <limits>
#include <vector>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/config/config-manager.hpp>

#include <wayland-server.h>
#include <wayfire/nonstd/wlroots.hpp>

namespace wf
{
class view_interface_t;
class toplevel_view_interface_t;
class window_manager_t;

namespace scene
{
class root_node_t;
}

namespace txn
{
class transaction_manager_t;
}

namespace touch
{
class gesture_t;
struct gesture_state_t;
}
}

using wayfire_view = nonstd::observer_ptr<wf::view_interface_t>;
using wayfire_toplevel_view = nonstd::observer_ptr<wf::toplevel_view_interface_t>;

namespace wf
{
class output_t;
class output_layout_t;
class input_device_t;
class bindings_repository_t;
class seat_t;

/** Describes the state of the compositor */
enum class compositor_state_t
{
    /** Not started */
    UNKNOWN,
    /**
     * The compositor core has finished initializing.
     * Now the wlroots backends are being started, which results in
     * adding of new input and output devices, as well as starting the
     * plugins on each output.
     */
    START_BACKEND,
    /**
     * The compositor has loaded the initial devices and plugins and is
     * running the main loop.
     */
    RUNNING,
    /**
     * The compositor has stopped the main loop and is shutting down.
     */
    SHUTDOWN,
};

class compositor_core_t : public wf::object_base_t, public signal::provider_t
{
  public:
    /**
     * The current configuration used by Wayfire
     */
    wf::config::config_manager_t config;

    /**
     * Command line arguments.
     */
    int argc;
    char **argv;

    /**
     * The wayland display and its event loop
     */
    wl_display *display;
    wl_event_loop *ev_loop;

    /**
     * The current wlr backend in use. The only case where another backend is
     * used is when there are no outputs added, in which case a noop backend is
     * used instead of this one
     */
    wlr_backend *backend;
    wlr_session *session;
    wlr_renderer *renderer;
    wlr_allocator *allocator;

    std::unique_ptr<wf::config_backend_t> config_backend;
    std::unique_ptr<wf::output_layout_t> output_layout;
    std::unique_ptr<wf::bindings_repository_t> bindings;
    std::unique_ptr<wf::seat_t> seat;
    std::unique_ptr<wf::txn::transaction_manager_t> tx_manager;
    std::unique_ptr<wf::window_manager_t> default_wm;

    /**
     * Various protocols supported by wlroots
     */
    struct
    {
        wlr_data_device_manager *data_device;
        wlr_data_control_manager_v1 *data_control;
        wlr_gamma_control_manager_v1 *gamma_v1;
        wlr_screencopy_manager_v1 *screencopy;
        wlr_export_dmabuf_manager_v1 *export_dmabuf;
        wlr_server_decoration_manager *decorator_manager;
        wlr_xdg_decoration_manager_v1 *xdg_decorator;
        wlr_xdg_output_manager_v1 *output_manager;
        wlr_virtual_keyboard_manager_v1 *vkbd_manager;
        wlr_virtual_pointer_manager_v1 *vptr_manager;
        wlr_input_inhibit_manager *input_inhibit;
        wlr_idle_notifier_v1 *idle_notifier;
        wlr_idle_inhibit_manager_v1 *idle_inhibit;
        wlr_pointer_gestures_v1 *pointer_gestures;
        wlr_relative_pointer_manager_v1 *relative_pointer;
        wlr_pointer_constraints_v1 *pointer_constraints;
        wlr_tablet_manager_v2 *tablet_v2;
        wlr_input_method_manager_v2 *input_method = NULL;
        wlr_text_input_manager_v3 *text_input     = NULL;
        wlr_presentation *presentation;
        wlr_primary_selection_v1_device_manager *primary_selection_v1;
        wlr_viewporter *viewporter;
        wlr_drm_lease_v1_manager *drm_v1;

        wlr_xdg_foreign_registry *foreign_registry;
        wlr_xdg_foreign_v1 *foreign_v1;
        wlr_xdg_foreign_v2 *foreign_v2;
    } protocols;

    std::string to_string() const
    {
        return "wayfire-core";
    }

    /**
     * @return the current seat. For now, Wayfire supports only a single seat,
     * which means get_current_seat() will always return the same (and only) seat.
     */
    virtual wlr_seat *get_current_seat() = 0;

    /** Set the cursor to the given name from the cursor theme, if available */
    virtual void set_cursor(std::string name) = 0;
    /**
     * Decrements the hide ref counter and unhides the cursor if it becomes 0.
     * */
    virtual void unhide_cursor() = 0;
    /**
     * Hides the cursor and increments the hide ref counter.
     * */
    virtual void hide_cursor() = 0;
    /**
     * Move the cursor to a specific position.
     * @param position the new position for the cursor, in global coordinates.
     */
    virtual void warp_cursor(wf::pointf_t position) = 0;

    /**
     * Break any grabs on pointer, touch and tablet input.
     * Then, transfer input focus to the given node in a grab mode.
     * Note that when transferring a grab, synthetic button release/etc. events are sent to the old pointer
     * and touch focus nodes (except if they are not RAW_INPUT nodes).
     *
     * The grab node, if it is not RAW_INPUT, will also not receive button release events for buttons pressed
     * before it grabbed the input, if it does not have the RAW_INPUT flag.
     *
     * @param node The node which should receive the grabbed input.
     */
    virtual void transfer_grab(wf::scene::node_ptr node) = 0;

    /** no such coordinate will ever realistically be used for input */
    static constexpr double invalid_coordinate =
        std::numeric_limits<double>::quiet_NaN();

    /**
     * @return The current cursor position in global coordinates or
     * {invalid_coordinate, invalid_coordinate} if no cursor.
     */
    virtual wf::pointf_t get_cursor_position() = 0;

    /**
     * @deprecated, use get_touch_state() instead
     *
     * @return The current position of the given touch point, or
     * {invalid_coordinate,invalid_coordinate} if it is not found.
     */
    virtual wf::pointf_t get_touch_position(int id) = 0;

    /**
     * @return The current state of all touch points.
     */
    virtual const wf::touch::gesture_state_t& get_touch_state() = 0;

    /**
     * @return The surface which has the cursor focus, or null if none.
     */
    virtual wf::scene::node_ptr get_cursor_focus() = 0;

    /**
     * @return The surface which has touch focus, or null if none.
     */
    virtual wf::scene::node_ptr get_touch_focus() = 0;

    /** @return The view whose surface is cursor focus */
    wayfire_view get_cursor_focus_view();
    /** @return The view whose surface is touch focus */
    wayfire_view get_touch_focus_view();
    /**
     * @return The view whose surface is under the given global coordinates,
     *  or null if none */
    wayfire_view get_view_at(wf::pointf_t point);

    /**
     * @return A list of all currently attached input devices.
     */
    virtual std::vector<nonstd::observer_ptr<wf::input_device_t>> get_input_devices()
    = 0;

    /**
     * @return the wlr_cursor used for the input devices
     */
    virtual wlr_cursor *get_wlr_cursor() = 0;

    /**
     * Register a new touchscreen gesture.
     */
    virtual void add_touch_gesture(
        nonstd::observer_ptr<wf::touch::gesture_t> gesture) = 0;

    /**
     * Unregister a touchscreen gesture.
     */
    virtual void rem_touch_gesture(
        nonstd::observer_ptr<wf::touch::gesture_t> gesture) = 0;

    /**
     * @deprecated. Use tracking_allocator_t<view_interface_t>::get_all()
     *
     * @return A list of all views core manages, regardless of their output,
     *  properties, etc.
     */
    std::vector<wayfire_view> get_all_views();

    /** The wayland socket name of Wayfire */
    std::string wayland_display;

    /**
     * Return the xwayland display name.
     *
     * @return The xwayland display name, or empty string if xwayland is not
     *   available.
     */
    virtual std::string get_xwayland_display() = 0;

    /**
     * Execute the given command in a POSIX shell. (/bin/sh)
     *
     * This also sets some environment variables for the new process, including
     * correct WAYLAND_DISPLAY and DISPLAY.
     *
     * @return The PID of the started client, or -1 on failure.
     */
    virtual pid_t run(std::string command) = 0;

    /**
     * @return The current state of the compositor.
     */
    virtual compositor_state_t get_current_state() = 0;

    /**
     * Shut down the whole compositor.
     *
     * Stops event loops, destroys outputs, views, etc.
     */
    virtual void shutdown() = 0;

    /**
     * Get the root node of Wayfire's scenegraph.
     */
    virtual const std::shared_ptr<scene::root_node_t>& scene() = 0;

    /**
     * Returns a reference to the only core instance.
     */
    static compositor_core_t& get();

  protected:
    compositor_core_t();
    virtual ~compositor_core_t();
};

/**
 * Change the view's output to new_output. If the reconfigure flag is
 * set, it will adjust the view geometry for the new output and clamp
 * it to the output geometry so it is at an expected size and position.
 */
void move_view_to_output(wayfire_toplevel_view v, wf::output_t *new_output, bool reconfigure);

/**
 * Simply a convenience function to call wf::compositor_core_t::get()
 */
compositor_core_t& get_core();
}

#endif // CORE_HPP
