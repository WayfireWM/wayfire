#ifndef WF_SEAT_POINTER_HPP
#define WF_SEAT_POINTER_HPP

#include <cmath>
#include <set>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/surface.hpp>
#include <wayfire/util.hpp>
#include <wayfire/option-wrapper.hpp>
#include "surface-map-state.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>

namespace wf
{
class input_manager_t;
class seat_t;
/**
 * Represents the "mouse cursor" part of a wf_cursor, i.e functionality provided
 * by touchpads, regular mice, trackpoints and similar.
 *
 * It is responsible for managing the focused surface and processing input
 * events from the aforementioned devices.
 */
class pointer_t
{
  public:
    pointer_t(nonstd::observer_ptr<wf::input_manager_t> input,
        nonstd::observer_ptr<seat_t> seat);
    ~pointer_t();

    /**
     * Enable/disable the logical pointer's focusing abilities.
     * The requests are counted, i.e if set_enable_focus(false) is called twice,
     * set_enable_focus(true) must be called also twice to restore focus.
     *
     * When a logical pointer is disabled, it means that no input surface can
     * receive pointer focus.
     */
    void set_enable_focus(bool enabled = true);

    /** Get the currenntlly set cursor focus */
    wf::surface_interface_t *get_focus() const;

    /**
     * Set the active pointer constraint
     *
     * @param last_destroyed In case a constraint is destroyed, the constraint
     * should be set to NULL, but this requires special handling, because not
     * all operations are supported on destroyed constraints
     */
    void set_pointer_constraint(wlr_pointer_constraint_v1 *constraint,
        bool last_destroyed = false);

    /**
     * Calculate the point inside the constraint region closest to the given
     * point.
     *
     * @param point The point to be constrained inside the region.
     * @return The closest point
     */
    wf::pointf_t constrain_point(wf::pointf_t point);

    /** @return The currently active pointer constraint */
    wlr_pointer_constraint_v1 *get_active_pointer_constraint();

    /** Handle events coming from the input devices */
    void handle_pointer_axis(wlr_pointer_axis_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_motion(wlr_pointer_motion_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_motion_absolute(wlr_pointer_motion_absolute_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_button(wlr_pointer_button_event *ev,
        input_event_processing_mode_t mode);

    /** Handle touchpad gestures detected by libinput */
    void handle_pointer_swipe_begin(wlr_pointer_swipe_begin_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_swipe_update(wlr_pointer_swipe_update_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_swipe_end(wlr_pointer_swipe_end_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_pinch_begin(wlr_pointer_pinch_begin_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_pinch_update(wlr_pointer_pinch_update_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_pinch_end(wlr_pointer_pinch_end_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_frame();

    /** Whether there are pressed buttons currently */
    bool has_pressed_buttons() const;

  private:
    nonstd::observer_ptr<wf::input_manager_t> input;
    nonstd::observer_ptr<seat_t> seat;

    // Buttons sent to the client currently
    // Note that count_pressed_buttons also contains buttons not sent to the
    // client
    std::multiset<uint32_t> currently_sent_buttons;

    SurfaceMapStateListener on_surface_map_state_change;

    wf::signal_callback_t on_views_updated;

    /** The surface which currently has cursor focus */
    wf::surface_interface_t *cursor_focus = nullptr;
    /** Whether focusing is enabled */
    int focus_enabled_count = 1;
    bool focus_enabled() const;

    /**
     * Set the pointer focus.
     *
     * @param surface The surface which should receive focus
     * @param local   The coordinates of the pointer relative to surface.
     *                No meaning if the surface is nullptr
     */
    void update_cursor_focus(wf::surface_interface_t *surface, wf::pointf_t local);

    /**
     * Handle an update of the cursor's position, which includes updating the
     * surface currently under the pointer.
     *
     * @param time_msec The time when the event causing this update occurred
     * @param real_update Whether the update is caused by a hardware event or
     *                    was artificially generated.
     */
    void update_cursor_position(uint32_t time_msec, bool real_update = true);

    /** Number of currently-pressed mouse buttons */
    int count_pressed_buttons = 0;
    wf::region_t constraint_region;
    wlr_pointer_constraint_v1 *active_pointer_constraint = nullptr;

    /** Figure out the global position of the given point.
     * @param relative The point relative to the cursor focus */
    wf::pointf_t get_absolute_position_from_relative(wf::pointf_t relative);

    /** Check whether an implicit grab should start/end */
    void check_implicit_grab();

    /** Implicitly grabbed surface when a button is being held */
    wf::surface_interface_t *grabbed_surface = nullptr;

    /** Set the currently grabbed surface
     * @param surface The surface to be grabbed, or nullptr to reset grab */
    void grab_surface(wf::surface_interface_t *surface);

    /** Send a button event to the currently active receiver, i.e to the
     * active input grab(if any), or to the focused surface */
    void send_button(wlr_pointer_button_event *ev, bool has_binding);

    /**
     * Send a motion event to the currently active receiver, i.e to the
     * active grab or the focused surface.
     *
     * @param local The coordinates of the cursor relative to the current
     * focus
     */
    void send_motion(uint32_t time_msec, wf::pointf_t local);
};
}

#endif /* end of include guard: WF_SEAT_POINTER_HPP */
