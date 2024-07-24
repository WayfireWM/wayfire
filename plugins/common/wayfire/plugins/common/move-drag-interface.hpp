#pragma once

#include "wayfire/toplevel-view.hpp"
#include "wayfire/geometry.hpp"
#include <memory>
#include <wayfire/object.hpp>
#include <wayfire/signal-provider.hpp>


namespace wf
{
/**
 * A collection of classes and interfaces which can be used by plugins which
 * support dragging views to move them.
 *
 *  A plugin using these APIs would get support for:
 *
 * - Moving views on the same output, following the pointer or touch position.
 * - Holding views in place until a certain threshold is reached
 * - Wobbly windows (if enabled)
 * - Move the view freely between different outputs with different plugins active
 *   on them, as long as all of these plugins support this interface.
 * - Show smooth transitions of the moving view when moving between different
 *   outputs.
 *
 * A plugin using these APIs is expected to:
 * - Grab input on its respective output and forward any events to the core_drag_t
 *   singleton.
 * - Have activated itself with CAPABILITY_MANAGE_COMPOSITOR
 * - Connect to and handle the signals described below.
 */
namespace move_drag
{
/**
 * name: focus-output
 * on: core_drag_t
 * when: Emitted output whenever the output where the drag happens changes,
 *   including when the drag begins.
 */
struct drag_focus_output_signal
{
    /** The output which was focused up to now, might be null. */
    wf::output_t *previous_focus_output;
    /** The output which was focused now. */
    wf::output_t *focus_output;
};

/**
 * Emitted on core_drag_t when input motion is triggered.
 */
struct drag_motion_signal
{
    wf::point_t current_position;
};

/**
 * name: snap-off
 * on: core_drag_t
 * when: Emitted if snap-off is enabled and the view was moved more than the
 *   threshold.
 */
struct snap_off_signal
{
    /** The output which is focused now. */
    wf::output_t *focus_output;
};

/**
 * name: done
 * on: core_drag_t
 * when: Emitted after the drag operation has ended, and if the view is unmapped
 *   while being dragged.
 */
struct drag_done_signal
{
    /** The output where the view was dropped. */
    wf::output_t *focused_output;

    /** Whether join-views was enabled for this drag. */
    bool join_views;

    struct view_t
    {
        /** Dragged view. */
        wayfire_toplevel_view view;

        /**
         * The position relative to the view where the grab was.
         * See scale_around_grab_t::relative_grab
         */
        wf::pointf_t relative_grab;
    };

    /** All views which were dragged. */
    std::vector<view_t> all_views;

    /** The main view which was dragged. */
    wayfire_toplevel_view main_view;

    /**
     * The position of the input when the view was dropped.
     * In output-layout coordinates.
     */
    wf::point_t grab_position;
};

/**
 * Find the position of grab relative to the view.
 * Example: returns [0.5, 0.5] if the grab is the midpoint of the view.
 */
inline static wf::pointf_t find_relative_grab(
    wf::geometry_t view, wf::point_t grab)
{
    return wf::pointf_t{
        1.0 * (grab.x - view.x) / view.width,
        1.0 * (grab.y - view.y) / view.height,
    };
}

inline std::vector<wayfire_toplevel_view> get_target_views(wayfire_toplevel_view grabbed, bool join_views)
{
    std::vector<wayfire_toplevel_view> r = {grabbed};
    if (join_views)
    {
        r = grabbed->enumerate_views();
    }

    return r;
}

struct drag_options_t
{
    /**
     * Whether to enable snap off, that is, hold the view in place until
     * a certain threshold is reached.
     */
    bool enable_snap_off = false;

    /**
     * If snap-off is enabled, the amount of pixels to wait for motion until
     * snap-off is triggered.
     */
    int snap_off_threshold = 0;

    /**
     * Join views together, i.e move main window and dialogues together.
     */
    bool join_views = false;

    double initial_scale = 1.0;
};

/**
 * An object for storing global move drag data (i.e shared between all outputs).
 *
 * Intended for use via wf::shared_data::ref_ptr_t.
 */
class core_drag_t : public signal::provider_t
{
    /**
     * Rebuild the wobbly model after a change in the scaling, so that the wobbly
     * model does not try to animate the scaling change itself.
     */
    void rebuild_wobbly(wayfire_toplevel_view view, wf::point_t grab, wf::pointf_t relative);

  public:
    std::optional<wf::point_t> tentative_grab_position;
    core_drag_t();
    ~core_drag_t();

    /**
     * A button has been pressed which might start a drag action.
     */
    template<class Point>
    void set_pending_drag(const Point& current_position)
    {
        this->tentative_grab_position = {(int)current_position.x, (int)current_position.y};
    }

    /**
     * Check whether a motion event makes a sufficient drag so that the drag operation may start at all.
     *
     * Note that in some cases this functionality is not used at all, if the action for example was triggered
     * by a binding.
     */
    bool should_start_pending_drag(wf::point_t current_position);

    /**
     * Start the actual dragging operation. Note: this should be called **after** set_pending_drag().
     *
     * @param grab_view The view which is being dragged.
     * @param grab_position The position of the input, in output-layout coordinates.
     * @param relative The position of the grab_position relative to view.
     */
    void start_drag(wayfire_toplevel_view grab_view, wf::pointf_t relative, const drag_options_t& options);
    void start_drag(wayfire_toplevel_view view, const drag_options_t& options);

    void handle_motion(wf::point_t to);

    double distance_to_grab_origin(wf::point_t to) const;
    void handle_input_released();
    void set_scale(double new_scale, double alpha = 1.0);

    bool is_view_held_in_place();

    // View currently being moved.
    wayfire_toplevel_view view;

    // Output where the action is happening.
    wf::output_t *current_output = NULL;

  private:
    struct impl;
    std::unique_ptr<impl> priv;


    void update_current_output(wf::point_t grab);
    void update_current_output(wf::output_t *output);
};

/**
 * Move the view to the target output and put it at the coordinates of the grab.
 * Also take into account view's fullscreen and tiled state.
 *
 * Unmapped views are ignored.
 */
void adjust_view_on_output(drag_done_signal *ev);

/**
 * Adjust the view's state after snap-off.
 */
void adjust_view_on_snap_off(wayfire_toplevel_view view);
}
}
