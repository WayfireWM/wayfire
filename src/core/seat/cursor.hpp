#ifndef CURSOR_HPP
#define CURSOR_HPP

#include "seat-impl.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/util.hpp"

namespace wf
{
struct cursor_t
{
    cursor_t(wf::seat_t *seat);
    ~cursor_t() = default;

    /**
     * Register a new input device.
     */
    void add_new_device(wlr_input_device *device);

    /**
     * Set the cursor image from a wlroots event.
     * @param validate_request Whether to validate the request against the
     * currently focused pointer surface, or not.
     */
    void set_cursor(const wlr_seat_pointer_request_set_cursor_event *ev,
        bool validate_request);
    void set_cursor(std::string name);
    void unhide_cursor();
    void hide_cursor();
    int hide_ref_counter = 0;

    /**
     * Delay setting the cursor, in order to avoid setting the cursor
     * multiple times in a single frame and to avoid setting it in the middle
     * of the repaint loop (not allowed by wlroots).
     */
    wf::wl_idle_call idle_set_cursor, idle_request_set_cursor;

    wlr_surface *last_surface;

    /**
     * Start/stop touchscreen mode, which means the cursor will be hidden.
     * It will be shown again once a pointer or tablet event happens.
     */
    void set_touchscreen_mode(bool enabled);

    /* Move the cursor to the given point */
    void warp_cursor(wf::pointf_t point);
    wf::pointf_t get_cursor_position();

    void init_xcursor();
    void setup_listeners();
    void load_xcursor_scale(float scale);

    // Device event listeners
    wf::wl_listener_wrapper on_button, on_motion, on_motion_absolute, on_axis,

        on_swipe_begin, on_swipe_update, on_swipe_end,
        on_pinch_begin, on_pinch_update, on_pinch_end,
        on_hold_begin, on_hold_end,

        on_tablet_tip, on_tablet_axis,
        on_tablet_button, on_tablet_proximity,
        on_frame;

    // Seat events
    wf::wl_listener_wrapper request_set_cursor, cursor_surface_destroyed, seat_client_destroyed;

    wf::signal::connection_t<wf::reload_config_signal> config_reloaded;
    wf::seat_t *seat;

    wlr_cursor *cursor = NULL;
    wlr_xcursor_manager *xcursor = NULL;

    bool touchscreen_mode_active = false;
};
}

#endif /* end of include guard: CURSOR_HPP */
