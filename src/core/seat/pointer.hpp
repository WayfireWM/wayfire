#ifndef WF_SEAT_POINTER_HPP
#define WF_SEAT_POINTER_HPP

#include <cmath>
#include <set>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/util.hpp>
#include <wayfire/option-wrapper.hpp>
#include "wayfire/scene-input.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
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
    wf::scene::node_ptr get_focus() const;

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
    void handle_pointer_hold_begin(wlr_pointer_hold_begin_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_hold_end(wlr_pointer_hold_end_event *ev,
        input_event_processing_mode_t mode);
    void handle_pointer_frame();

    /** Whether there are pressed buttons currently */
    bool has_pressed_buttons() const;

    /**
     * Handle an update of the cursor's position, which includes updating the
     * surface currently under the pointer.
     *
     * @param time_msec The time when the event causing this update occurred
     */
    void update_cursor_position(int64_t time_msec);

    /**
     * Transfer focus and pressed buttons to the given grab.
     */
    void transfer_grab(scene::node_ptr node);

  private:
    nonstd::observer_ptr<wf::input_manager_t> input;
    nonstd::observer_ptr<seat_t> seat;

    // Store the last motion / surface-local coords sent to the current focus.
    // This is useful to avoid sending repetitive motion events if we for ex. have scenegraph changes, but no
    // actual movement of the mouse.
    std::optional<wf::pointf_t> last_focus_coords;

    // Buttons sent to the client currently
    // Note that count_pressed_buttons also contains buttons not sent to the
    // client
    std::multiset<uint32_t> currently_sent_buttons;

    wf::signal::connection_t<wf::scene::root_node_update_signal>
    on_root_node_updated;

    /** The surface which currently has cursor focus */
    wf::scene::node_ptr cursor_focus = nullptr;
    /** Whether focusing is enabled */
    int focus_enabled_count = 1;
    bool focus_enabled() const;

    void send_enter_to_focus();

    /**
     * Set the pointer focus.
     */
    void update_cursor_focus(wf::scene::node_ptr node);

    /** Number of currently-pressed mouse buttons */
    int count_pressed_buttons = 0;

    /** Check whether an implicit grab should start/end */
    void check_implicit_grab();

    /** Implicitly grabbed node when a button is being held */
    wf::scene::node_ptr grabbed_node = nullptr;

    /** Set the currently grabbed node
     * @param node The node to be grabbed, or nullptr to reset grab */
    void grab_surface(wf::scene::node_ptr node);

    /** Send a button event to the currently active receiver, i.e to the
     * active input grab(if any), or to the focused surface */
    void send_button(wlr_pointer_button_event *ev, bool has_binding);

    /**
     * Send a motion event to the currently active receiver, i.e to the
     * active grab or the focused surface.
     */
    void send_motion(uint32_t time_msec);

    /**
     * Send synthetic button release events to the old cursor focus.
     */
    void send_leave_to_focus(wf::scene::node_ptr old_focus);
};
}

#endif /* end of include guard: WF_SEAT_POINTER_HPP */
