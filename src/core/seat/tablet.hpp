#ifndef WF_SEAT_TABLET_HPP
#define WF_SEAT_TABLET_HPP

#include <wayfire/util.hpp>
#include "seat.hpp"
#include "wayfire/signal-definitions.hpp"

namespace wf
{
struct tablet_tool_t
{
    /**
     * Create a new tablet tool.
     * It will automatically free its memory once the wlr object is destroyed.
     */
    tablet_tool_t(wlr_tablet_tool *tool, wlr_tablet_v2_tablet *tablet);
    ~tablet_tool_t();

    wlr_tablet_tool *tool;
    wlr_tablet_v2_tablet_tool *tool_v2;

    /**
     * Called whenever a refocus of the tool is necessary
     */
    void update_tool_position();

    /** Set the proximity surface */
    void set_focus(wf::surface_interface_t *surface);

    /**
     * Send the axis updates directly.
     * Only the position is handled separately.
     */
    void passthrough_axis(wlr_tablet_tool_axis_event *ev);

    /**
     * Called whenever a tip occurs for this tool
     */
    void handle_tip(wlr_tablet_tool_tip_event *ev);

    /** Handle a button event */
    void handle_button(wlr_tablet_tool_button_event *ev);

    /** Set proximity state */
    void handle_proximity(wlr_tablet_tool_proximity_event *ev);

  private:
    wf::wl_listener_wrapper on_destroy, on_set_cursor;
    wf::wl_listener_wrapper on_tool_v2_destroy;
    wf::signal_connection_t on_surface_map_state_changed;
    wf::signal_connection_t on_views_updated;

    /** Tablet that this tool belongs to */
    wlr_tablet_v2_tablet *tablet_v2;

    /** Surface where the tool is in */
    wf::surface_interface_t *proximity_surface = nullptr;
    /** Surface where the tool was grabbed */
    wf::surface_interface_t *grabbed_surface = nullptr;

    double tilt_x = 0.0;
    double tilt_y = 0.0;

    /* A tablet tool is active if it has a proximity_in
     * event but no proximity_out */
    bool is_active = false;
};

struct tablet_t : public input_device_impl_t
{
    /**
     * Create a new tablet tool for the given cursor.
     */
    tablet_t(wlr_cursor *cursor, wlr_input_device *tool);
    virtual ~tablet_t();

    /** Handle a tool tip event */
    void handle_tip(wlr_tablet_tool_tip_event *ev,
        input_event_processing_mode_t mode);
    /** Handle an axis event */
    void handle_axis(wlr_tablet_tool_axis_event *ev,
        input_event_processing_mode_t mode);
    /** Handle a button event */
    void handle_button(wlr_tablet_tool_button_event *ev,
        input_event_processing_mode_t mode);
    /** Handle a proximity event */
    void handle_proximity(wlr_tablet_tool_proximity_event *ev,
        input_event_processing_mode_t mode);

    wlr_tablet_v2_tablet *tablet_v2;

  private:
    wlr_tablet *handle;
    wlr_cursor *cursor;

    /**
     * Get the wayfire tool associated with the wlr tool.
     * The wayfire tool will be created if it doesn't exist yet.
     */
    tablet_tool_t *ensure_tool(wlr_tablet_tool *tool);
};

struct tablet_pad_t : public input_device_impl_t
{
  public:
    tablet_pad_t(wlr_input_device *pad);
    ~tablet_pad_t();

  private:
    wlr_tablet_v2_tablet_pad *pad_v2;

    /** The tablet this pad is attached to. */
    nonstd::observer_ptr<tablet_t> attached_to;

    wf::wl_listener_wrapper on_attach, on_button, on_strip, on_ring;
    signal_callback_t on_input_devices_changed, on_keyboard_focus_changed;

    /** Attach the pad to the given tablet. */
    void attach_to_tablet(tablet_t *tablet);
    /** Auto-select the tool to attach to, from the available devices */
    void select_default_tool();

    wlr_surface *old_focus = nullptr;

    /** Update the cursor focus */
    void update_focus(wlr_surface *focus);
    /** Update the cursor focus to the focused view */
    void update_focus();
};
}

#endif /* end of include guard: WF_SEAT_TABLET_HPP */
