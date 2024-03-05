#ifndef SIGNAL_DEFINITIONS_HPP
#define SIGNAL_DEFINITIONS_HPP

#include "wayfire/view.hpp"
#include "wayfire/output.hpp"

/**
 * Documentation of signals emitted from core components.
 * Each signal documentation follows the following scheme:
 *
 * name: The base name of the signal
 * on: Which components the plugin is emitted on. Prefixes are specified
 *     in (), i.e test(view-) means that the signal is emitted on component
 *     test with prefix 'view-'.
 * when: Description of when the signal is emitted.
 * argument: What the signal data represents when there is no dedicated
 *   signal data struct.
 */

namespace wf
{
/* ----------------------------------------------------------------------------/
 * Core signals
 * -------------------------------------------------------------------------- */
/**
 * on: core
 * when: Emitted when the wlroots backend has been started.
 */
struct core_backend_started_signal
{};

/**
 * on: core
 * when: Emitted when the Wayfire initialization has been completed and the main loop is about to start.
 */
struct core_startup_finished_signal
{};

/**
 * on: core
 * when: Right before the shutdown sequence starts.
 */
struct core_shutdown_signal
{};

class input_device_t;
/**
 * on: core
 * when: Whenever a new input device is added.
 */
struct input_device_added_signal
{
    nonstd::observer_ptr<input_device_t> device;
};

/**
 * on: core
 * when: Whenever an input device is removed.
 */
struct input_device_removed_signal
{
    nonstd::observer_ptr<input_device_t> device;
};

/**
 * on: core
 * when: When the corresponding switch device state changes.
 */
struct switch_signal
{
    /** The switch device */
    nonstd::observer_ptr<input_device_t> device;
    /** On or off */
    bool state;
};


/**
 * Describes the various ways in which core should handle an input event.
 */
enum class input_event_processing_mode_t
{
    /**
     * Core should process this event for input grabs, bindings and eventually
     * forward it to a client surface.
     */
    FULL,
    /**
     * Core should process this event for input grabs and bindings, but not send
     * the event to the client.
     */
    NO_CLIENT,
    /**
     * Core should not process this event at all.
     */
    IGNORE,
};

/**
 * Emitted for the following events:
 *   pointer_motion, pointer_motion_absolute, pointer_button, pointer_axis,
 *   pointer_swipe_begin, pointer_swipe_update, pointer_swipe_end, pointer_pinch_begin, pointer_pinch_update,
 *   pointer_pinch_end, pointer_hold_begin, pointer_hold_end,
 *   keyboard_key,
 *   touch_down, touch_up, touch_motion,
 *   tablet_proximity, tablet_axis, tablet_button, tablet_tip
 *
 * on: core
 * when: The input event signals are sent from core whenever a new input from an
 *   input device arrives. The events are sent before any processing is done,
 *   and they are independent of plugin input grabs and other wayfire input
 *   mechanisms.
 *
 *   The event data can be modified by plugins, and then the modified event
 *   will be used instead. However plugins which modify the event must ensure
 *   that subsequent events are adjusted accordingly as well.
 *
 * example: The pointer_motion event is emitted with data of type
 *   input_event_signal<wlr_pointer_motion_event>
 */
template<class wlr_event_t>
struct input_event_signal
{
    /* The event as it has arrived from wlroots */
    wlr_event_t *event;

    /**
     * Describes how core should handle this event.
     *
     * This is currently supported for only a subset of signals, namely:
     *
     * pointer_button, keyboard_key, touch_down
     */
    input_event_processing_mode_t mode = input_event_processing_mode_t::FULL;

    /**
     * The wlr device which triggered the event. May be NULL for events which are merged together for ex. via
     * wlr-cursor.
     */
    wlr_input_device *device;
};

/**
 * Same as @input_event_signal, but emitted after the event has been handled.
 */
template<class wlr_event_t>
struct post_input_event_signal
{
    wlr_event_t *event;
    wlr_input_device *device;
};

/**
 * on: core
 * when: When the config file is reloaded
 */
struct reload_config_signal
{};

/**
 * on: core
 * when: Keyboard focus is changed (may change to nullptr).
 */
struct keyboard_focus_changed_signal
{
    wf::scene::node_ptr new_focus;
};

/**
 * on: core
 * when: Seat activity has happened after being idle.
 */
struct seat_activity_signal
{};

/**
 * on: core
 * when: idle inhibit changed.
 */
struct idle_inhibit_changed_signal
{
    bool inhibit;
};

/* ----------------------------------------------------------------------------/
 * Output signals
 * -------------------------------------------------------------------------- */

/** Base class for all output signals. */

/**
 * on: output-layout
 * when: Each time a new output is added.
 */
struct output_added_signal
{
    wf::output_t *output;
};

/**
 * on: output, output-layout(output-)
 * when: Emitted just before starting the destruction procedure for an output.
 */
struct output_pre_remove_signal
{
    wf::output_t *output;
};

/**
 * on: output-layout
 * when: Each time a new output is added.
 */
struct output_removed_signal
{
    wf::output_t *output;
};

enum output_config_field_t
{
    /** Output source changed */
    OUTPUT_SOURCE_CHANGE    = (1 << 0),
    /** Output mode changed */
    OUTPUT_MODE_CHANGE      = (1 << 1),
    /** Output scale changed */
    OUTPUT_SCALE_CHANGE     = (1 << 2),
    /** Output transform changed */
    OUTPUT_TRANSFORM_CHANGE = (1 << 3),
    /** Output position changed */
    OUTPUT_POSITION_CHANGE  = (1 << 4),
};

struct output_state_t;

/**
 * on: output-layout
 * when: Each time the configuration of the output layout changes.
 */
struct output_layout_configuration_changed_signal
{};

/**
 * on: output
 * when: Each time the output's source, mode, scale, transform and/or position changes.
 */
struct output_configuration_changed_signal
{
    wf::output_t *output;
    output_configuration_changed_signal(const wf::output_state_t& st) : state(st)
    {}

    /**
     * Which output attributes actually changed.
     * A bitwise OR of output_config_field_t.
     */
    uint32_t changed_fields;

    /**
     * The new state of the output.
     */
    const wf::output_state_t& state;
};

/**
 * on: output, core(output-)
 * when: Immediately after the output becomes focused.
 */
struct output_gain_focus_signal
{
    wf::output_t *output;
};

/**
 * on: output, core
 * when: When an output activates or deactivates. Note that not all plugin actions are reflected with this
 *   signal. A plugin activates on an output usually if it (temporarily) changes the way Wayfire works like
 *   Expo, Scale, Vswitch. One-shot actions like command or wsets do not send this signal.
 */
struct output_plugin_activated_changed_signal
{
    // The output on which the plugin was activated. May be NULL if the plugin works globally.
    wf::output_t *output;
    // The name of the plugin.
    std::string plugin_name;
    // Whether the plugin was activated (true) or deactivated (false).
    bool activated;
};

/* ----------------------------------------------------------------------------/
 * Output rendering signals (see also wayfire/workspace-stream.hpp)
 * -------------------------------------------------------------------------- */
/**
 * on: output
 * when: Whenever the output is ready to start rendering. This can happen
 *   either on output creation or whenever all inhibits in wayfire-shell have
 *   been removed.
 */
struct output_start_rendering_signal
{
    wf::output_t *output;
};

/* ----------------------------------------------------------------------------/
 * Output workspace signals
 * -------------------------------------------------------------------------- */

/**
 * on: output
 * when: Whenever the current workspace set of the output changes.
 */
struct workspace_set_changed_signal
{
    std::shared_ptr<workspace_set_t> new_wset;
    wf::output_t *output;
};

/**
 * on: workspace set, output
 * when: Whenever the current workspace on the output has changed.
 */
struct workspace_changed_signal
{
    /** Previously focused workspace */
    wf::point_t old_viewport;

    /** Workspace that is to be focused or became focused */
    wf::point_t new_viewport;

    /** The output this is happening on */
    wf::output_t *output;
};

/**
 * on: output
 * when: Whenever a workspace change is requested by core or by a plugin.
 *   This can be used by plugins who wish to handle workspace changing
 *   themselves, for ex. if animating the transition.
 */
struct workspace_change_request_signal
{
    /** Previously focused workspace */
    wf::point_t old_viewport;

    /** Workspace that is to be focused or became focused */
    wf::point_t new_viewport;

    /** The output this is happening on */
    wf::output_t *output;

    /** Whether the request has already been handled. */
    bool carried_out;

    /**
     * A list of views whose geometry should remain stationary.
     * The caller is responsible for ensuring that this doesn't move the views
     * outside of the visible area.
     *
     * Note that the views might still be moved if a previous workspace change
     * request is being serviced.
     */
    std::vector<wayfire_toplevel_view> fixed_views;
};

/**
 * on: workspace set
 * when: Whenever the workspace grid size changes.
 */
struct workspace_grid_changed_signal
{
    /** The grid size before the change. */
    wf::dimensions_t old_grid_size;

    /** The grid size after the change. */
    wf::dimensions_t new_grid_size;
};

/**
 * on: output
 * when: Whenever the available workarea changes.
 */
struct workarea_changed_signal
{
    wf::output_t *output;
    wf::geometry_t old_workarea;
    wf::geometry_t new_workarea;
};

/**
 * on: output
 * when: Whenever a fullscreen view is promoted on top of the other layers.
 */
struct fullscreen_layer_focused_signal
{
    bool has_promoted;
};

/* ----------------------------------------------------------------------------/
 * View signals
 * -------------------------------------------------------------------------- */
/**
 * on: view, output, core
 * when: After the view becomes mapped. This signal must also be emitted from all compositor views.
 */
struct view_mapped_signal
{
    wayfire_view view;

    /* Indicates whether the position already has its initial position */
    bool is_positioned = false;
};

/**
 * on: view, output, core
 * when: Immediately before unmapping a mapped view. The signal may not be
 *   emitted from all views, but it is necessary for unmap animations to work.
 */
struct view_pre_unmap_signal
{
    wayfire_view view;
};

/**
 * name: unmapped
 * on: view, output, core
 * when: After a previously mapped view becomes unmapped. This must be emitted
 *   for all views.
 */
struct view_unmapped_signal
{
    wayfire_view view;
};

/**
 * on: view, new output, core
 * when: Immediately after the view's output changes. Note that child views may still be on the old output.
 */
struct view_set_output_signal
{
    wayfire_view view;
    // The previous output of the view.
    wf::output_t *output;
};

/* ----------------------------------------------------------------------------/
 * View state signals
 * -------------------------------------------------------------------------- */
/**
 * on: view
 * when: After the view's parent changes.
 */
struct view_parent_changed_signal
{};

/**
 * on: view, output(view-)
 * when: After the view's minimized state changes.
 */
struct view_minimized_signal
{
    wayfire_toplevel_view view;
};

/**
 * on: output
 * when: Emitted whenever some entity requests that the view's minimized state
 *   changes. If no plugin is available to service the request, it is carried
 *   out by core. See view_interface_t::minimize_request()
 */
struct view_minimize_request_signal
{
    wayfire_toplevel_view view;

    /** true is minimized, false is restored */
    bool state;

    /**
     * Whether some plugin will service the minimization request, in which
     * case other plugins and core should ignore the request.
     */
    bool carried_out = false;
};

/**
 * on: view
 * when: After the view's activated state changes.
 */
struct view_activated_state_signal
{};

/**
 * on: view, output(view-)
 * when: After the view's tiled edges change.
 */
struct view_tiled_signal
{
    wayfire_toplevel_view view;

    /** Previously tiled edges */
    uint32_t old_edges;
    /** Currently tiled edges */
    uint32_t new_edges;
};

/**
 * on: output
 * when: Emitted whenever some entity requests that the view's tiled edges
 *   change. If no plugin is available to service the request, it is carried
 *   out by core. See view_interface_t::tile_request()
 */
struct view_tile_request_signal
{
    wayfire_toplevel_view view;

    /** The desired edges */
    uint32_t edges;

    /**
     * The geometry the view should have. This is for example the last geometry
     * a view had before being tiled.  The given geometry is only a hint by core
     * and plugins may override it. It may also be undefined (0,0 0x0).
     */
    wf::geometry_t desired_size;

    /**
     * The target workspace of the operation.
     */
    wf::point_t workspace;

    /**
     * Whether some plugin will service the tile request, in which case other
     * plugins and core should ignore the request.
     */
    bool carried_out = false;
};

/**
 * on: view, output(view-)
 * when: After the view's fullscreen state changes.
 */
struct view_fullscreen_signal
{
    wayfire_toplevel_view view;
    bool state;
};

/**
 * on: output
 * when: Emitted whenever some entity requests that the view's fullscreen state
 *   change. If no plugin is available to service the request, it is carried
 *   out by core. See view_interface_t::fullscreen_request()
 */
struct view_fullscreen_request_signal
{
    wayfire_toplevel_view view;

    /** The desired fullscreen state */
    bool state;

    /**
     * Whether some plugin will service the fullscreen request, in which case
     * other plugins and core should ignore the request.
     */
    bool carried_out = false;

    /**
     * The geometry the view should have. This is for example the last geometry
     * a view had before being fullscreened. The given geometry is only a hint
     * by core and plugins may override it. It may also be undefined (0,0 0x0).
     */
    wf::geometry_t desired_size;

    /**
     * The target workspace of the operation.
     */
    wf::point_t workspace;
};


/**
 * on: view, core
 * when: Emitted whenever some entity (typically a panel) wants to focus the view.
 */
struct view_focus_request_signal
{
    wayfire_view view;

    /** Set to true if core and other plugins should not handle this request. */
    bool carried_out = false;

    /** Set to true if the request comes from the view client itself */
    bool self_request;
};

/**
 * name: set-sticky
 * on: view, output(view-)
 * when: Whenever the view's sticky state changes.
 */
struct view_set_sticky_signal
{
    wayfire_toplevel_view view;
};

/**
 * on: view, core
 * when: After the view's title has changed.
 */
struct view_title_changed_signal
{
    wayfire_view view;
};

/**
 * on: view, core
 * when: After the view's app-id has changed.
 */
struct view_app_id_changed_signal
{
    wayfire_view view;
};

/**
 * on: output, core
 * when: To show a menu with window related actions.
 */
struct view_show_window_menu_signal
{
    wayfire_view view;

    /** The position as requested by the client, in surface coordinates */
    wf::point_t relative_position;
};

/**
 * on: view, output(view-), core(view-)
 * when: Whenever the view's wm geometry changes.
 */
struct view_geometry_changed_signal
{
    wayfire_toplevel_view view;

    /** The old wm geometry */
    wf::geometry_t old_geometry;
};

/**
 * on: output
 * when: Whenever the view's workspace changes. (Every plugin changing the
 *   view's workspace should emit this signal).
 */
struct view_change_workspace_signal
{
    wayfire_toplevel_view view;

    wf::point_t from, to;

    /**
     * Indicates whether the old workspace is known.
     * If false, then the `from` field should be ignored.
     */
    bool old_workspace_valid = true;
};

/**
 * on: view, core
 * when: Whenever the value of view::should_be_decorated() changes.
 */
struct view_decoration_state_updated_signal
{
    wayfire_toplevel_view view;
};

/**
 * on: view
 * when: Whenever the client fails to respond to a ping request within
 *   the expected time(10 seconds).
 */
struct view_ping_timeout_signal
{
    wayfire_view view;
};

/* ----------------------------------------------------------------------------/
 * View <-> output signals
 * -------------------------------------------------------------------------- */

/**
 * on: core
 * when: Immediately before the view is moved to a different workspace set.
 *   view-moved-to-set is emitted afterwards.
 */
struct view_pre_moved_to_wset_signal
{
    /* The view being moved */
    wayfire_toplevel_view view;
    /* The old wset the view was on, may be NULL. */
    std::shared_ptr<wf::workspace_set_t> old_wset;
    /* The new wset the view is being moved to, may be NULL. */
    std::shared_ptr<wf::workspace_set_t> new_wset;
};

/**
 * on: core
 * when: After the view has been moved to a new wset.
 */
struct view_moved_to_wset_signal
{
    /* The view being moved */
    wayfire_toplevel_view view;
    /* The old wset the view was on, may be NULL. */
    std::shared_ptr<wf::workspace_set_t> old_wset;
    /* The new wset the view is being moved to, may be NULL. */
    std::shared_ptr<wf::workspace_set_t> new_wset;
};

/**
 * on: output
 * when: This signal is a combination of the unmapped, minimized and set-output signals. In the latter case,
 *   the signal is emitted on the view's previous output. The meaning of this signal is that the view is no
 *   longer available for focus, interaction with the user, etc. on the output where it used to be.
 */
struct view_disappeared_signal
{
    wayfire_view view;
};

/**
 * on: output
 * when: Whenever an interactive move is requested on the view. See also
 *   view_interface_t::move_request()
 */
struct view_move_request_signal
{
    wayfire_toplevel_view view;
};

/**
 * on: output
 * when: Whenever an interactive resize is requested on the view. See also
 *   view_interface_t::resize_request()
 */
struct view_resize_request_signal
{
    wayfire_toplevel_view view;

    /** The requested resize edges */
    uint32_t edges;
};

/**
 * on: view and core(view-)
 * when: the client indicates the views hints have changed (example urgency hint).
 */
struct view_hints_changed_signal
{
    wayfire_view view;

    bool demands_attention = false;
};

/**
 * on: core
 * when: Whenever a client wants to invoke the system bell if such is available.
 *   Note the system bell may or may not be tied to a particular view, so the
 *   signal may be emitted with a nullptr view.
 */
struct view_system_bell_signal
{
    wayfire_view view;
};
}

#endif
