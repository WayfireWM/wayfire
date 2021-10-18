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
    wf::focused_view_t get_focus() const;

    /** Handle events coming from the input devices */
    void handle_pointer_axis(wlr_event_pointer_axis *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_motion(wlr_event_pointer_motion *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_motion_absolute(wlr_event_pointer_motion_absolute *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_button(wlr_event_pointer_button *ev,
        input_event_processing_mode_t mode);

    /** Handle touchpad gestures detected by libinput */
    void handle_pointer_swipe_begin(wlr_event_pointer_swipe_begin *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_swipe_update(wlr_event_pointer_swipe_update *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_swipe_end(wlr_event_pointer_swipe_end *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_pinch_begin(wlr_event_pointer_pinch_begin *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_pinch_update(wlr_event_pointer_pinch_update *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_pinch_end(wlr_event_pointer_pinch_end *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_frame();

    /** Whether there are pressed buttons currently */
    bool has_pressed_buttons() const;

    /**
     * Handle an update of the cursor's position, which includes updating the
     * surface currently under the pointer.
     *
     * @param time_msec The time when the event causing this update occurred
     * @param real_update Whether the update is caused by a hardware event or
     *                    was artificially generated.
     */
    void update_cursor_position(uint32_t time_msec, bool real_update = true);

  private:
    nonstd::observer_ptr<wf::input_manager_t> input;
    nonstd::observer_ptr<seat_t> seat;

    // Buttons sent to the client currently
    // Note that count_pressed_buttons also contains buttons not sent to the
    // client
    std::multiset<uint32_t> currently_sent_buttons;

    SurfaceMapStateListener on_surface_map_state_change;

    wf::signal_connection_t on_views_updated;

    /** The surface which currently has cursor focus */
    wf::focused_view_t cursor_focus = {nullptr, nullptr};
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
    void update_cursor_focus(wf::focused_view_t surface, wf::pointf_t local);

    /** Number of currently-pressed mouse buttons */
    int count_pressed_buttons = 0;
    std::optional<wf::region_t> active_constraint_region;

    /** Figure out the global position of the given point.
     * @param relative The point relative to the cursor focus */
    wf::pointf_t get_absolute_position_from_relative(wf::pointf_t relative);

    /** Check whether an implicit grab should start/end */
    void check_implicit_grab();

    /** Implicitly grabbed surface when a button is being held */
    wf::focused_view_t grabbed_surface;

    /** Set the currently grabbed surface
     * @param surface The surface to be grabbed, or nullptr to reset grab */
    void grab_surface(wf::focused_view_t focus);

    /** Send a button event to the currently active receiver, i.e to the
     * active input grab(if any), or to the focused surface */
    void send_button(wlr_event_pointer_button *ev, bool has_binding);

    /**
     * Send a motion event to the currently active receiver, i.e to the
     * active grab or the focused surface.
     *
     * @param local The coordinates of the cursor relative to the current
     * focus
     */
    void send_motion(uint32_t time_msec, wf::pointf_t local);

    /**
     * Send release for all currently pressed buttons.
     */
    void release_all_buttons();

    /**
     * Set the active constraint relative to the currently focused surface.
     */
    void set_pointer_constraint(std::optional<wf::region_t> region);

    /**
     * Calculate the point inside the constraint region closest to the given
     * point.
     *
     * @param point The point to be constrained inside the region.
     * @return The closest point
     */
    wf::pointf_t constrain_point(wf::pointf_t point);
};
}

#endif /* end of include guard: WF_SEAT_POINTER_HPP */
