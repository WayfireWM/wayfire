#pragma once

#include "wayfire/scene.hpp"
#include <wayfire/nonstd/reverse.hpp>
#include <wayfire/plugins/wobbly/wobbly-signal.hpp>
#include <wayfire/object.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/util/log.hpp>
#include <cmath>
#include <wayfire/nonstd/wlroots-full.hpp>


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
struct drag_focus_output_signal : public signal_data_t
{
    /** The output which was focused up to now, might be null. */
    wf::output_t *previous_focus_output;
    /** The output which was focused now. */
    wf::output_t *focus_output;
};

/**
 * name: snap-off
 * on: core_drag_t
 * when: Emitted if snap-off is enabled and the view was moved more than the
 *   threshold.
 */
struct snap_off_signal : public signal_data_t
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
struct drag_done_signal : public signal_data_t
{
    /** The output where the view was dropped. */
    wf::output_t *focused_output;

    /** Whether join-views was enabled for this drag. */
    bool join_views;

    struct view_t
    {
        /** Dragged view. */
        wayfire_view view;

        /**
         * The position relative to the view where the grab was.
         * See scale_around_grab_t::relative_grab
         */
        wf::pointf_t relative_grab;
    };

    /** All views which were dragged. */
    std::vector<view_t> all_views;

    /** The main view which was dragged. */
    wayfire_view main_view;

    /**
     * The position of the input when the view was dropped.
     * In output-layout coordinates.
     */
    wf::point_t grab_position;
};

/**
 * Find the geometry of a view, if it has size @size, it is grabbed at point @grab,
 * and the grab is at position @relative relative to the view.
 */
inline static wf::geometry_t find_geometry_around(
    wf::dimensions_t size, wf::point_t grab, wf::pointf_t relative)
{
    return wf::geometry_t{
        grab.x - (int)std::floor(relative.x * size.width),
        grab.y - (int)std::floor(relative.y * size.height),
        size.width,
        size.height,
    };
}

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

/**
 * A transformer used while dragging.
 *
 * It is primarily used to scale the view is a plugin needs it, and also to keep it
 * centered around the `grab_position`.
 */
class scale_around_grab_t : public wf::view_transformer_t
{
  public:
    /**
     * Factor for scaling down the view.
     * A factor 2.0 means that the view will have half of its width and height.
     */
    wf::animation::simple_animation_t scale_factor{wf::create_option(300)};

    /**
     * A place relative to the view, where it is grabbed.
     *
     * Coordinates are [0, 1]. A grab at (0.5, 0.5) means that the view is grabbed
     * at its center.
     */
    wf::pointf_t relative_grab;

    /**
     * The position where the grab appears on the outputs, in output-layout
     * coordinates.
     */
    wf::point_t grab_position;

    uint32_t get_z_order() override
    {
        return wf::TRANSFORMER_HIGHLEVEL - 1;
    }

    wf::region_t transform_opaque_region(
        wf::geometry_t box, wf::region_t region) override
    {
        // TODO: figure out a way to take opaque region into account
        return {};
    }

    wf::pointf_t scale_around_grab(wf::geometry_t view, wf::pointf_t point,
        double factor)
    {
        auto gx = view.x + view.width * relative_grab.x;
        auto gy = view.y + view.height * relative_grab.y;

        return {
            (point.x - gx) * factor + gx,
            (point.y - gy) * factor + gy,
        };
    }

    wf::pointf_t transform_point(wf::geometry_t view, wf::pointf_t point) override
    {
        LOGE("Unexpected transform_point() call for dragged overlay view!");
        return scale_around_grab(view, point, 1.0 / scale_factor);
    }

    wf::pointf_t untransform_point(wf::geometry_t view, wf::pointf_t point) override
    {
        LOGE("Unexpected untransform_point() call for dragged overlay view!");
        return scale_around_grab(view, point, scale_factor);
    }

    wf::geometry_t get_bounding_box(wf::geometry_t view,
        wf::geometry_t region) override
    {
        int w = std::floor(view.width / scale_factor);
        int h = std::floor(view.height / scale_factor);

        auto bb = find_geometry_around({w, h}, grab_position, relative_grab);
        return bb;
    }

    void render_with_damage(wf::texture_t src_tex, wlr_box src_box,
        const wf::region_t& damage, const wf::render_target_t& target_fb) override
    {
        // Get target size
        auto bbox = get_bounding_box(src_box, src_box);

        OpenGL::render_begin(target_fb);
        for (auto& rect : damage)
        {
            target_fb.logic_scissor(wlr_box_from_pixman_box(rect));
            OpenGL::render_texture(src_tex, target_fb, bbox);
        }

        OpenGL::render_end();
    }
};

static const std::string move_drag_transformer = "move-drag-transformer";

/**
 * Represents a view which is being dragged.
 * Multiple views exist only if join_views is set to true.
 */
struct dragged_view_t
{
    // The view being dragged
    wayfire_view view;

    // Its transformer
    nonstd::observer_ptr<scale_around_grab_t> transformer;

    // The last bounding box used for damage.
    // This is needed in case the view resizes or something like that, in which
    // case we don't have access to the previous bbox.
    wf::geometry_t last_bbox;
};

inline wayfire_view get_toplevel(wayfire_view view)
{
    while (view->parent)
    {
        view = view->parent;
    }

    return view;
}

inline std::vector<wayfire_view> get_target_views(wayfire_view grabbed,
    bool join_views)
{
    std::vector<wayfire_view> r = {grabbed};
    if (join_views)
    {
        r = grabbed->enumerate_views();
    }

    return r;
}

/**
 * An object for storing per-output data.
 */
class output_data_t : public custom_data_t
{
  public:
    output_data_t(wf::output_t *output, std::vector<dragged_view_t> views)
    {
        output->render->add_effect(&damage_overlay, OUTPUT_EFFECT_PRE);
        output->render->add_effect(&render_overlay, OUTPUT_EFFECT_OVERLAY);

        this->output = output;
        this->views  = views;
    }

    ~output_data_t()
    {
        output->render->rem_effect(&damage_overlay);
        output->render->rem_effect(&render_overlay);
    }

    output_data_t(const output_data_t &) = delete;
    output_data_t(output_data_t &&) = delete;
    output_data_t& operator =(const output_data_t&) = delete;
    output_data_t& operator =(output_data_t&&) = delete;

    void apply_damage()
    {
        for (auto& view : views)
        {
            // Note: bbox will be in output layout coordinates now, since this is
            // how the transformer works
            auto bbox = view.view->get_bounding_box();
            bbox = bbox + -wf::origin(output->get_layout_geometry());

            output->render->damage(bbox);
            output->render->damage(view.last_bbox);

            view.last_bbox = bbox;
        }
    }

  private:
    wf::output_t *output;

    std::vector<dragged_view_t> views;

    // An effect hook for damaging the view on the current output.
    //
    // This is needed on a per-output basis in order to drive the scaling animation
    // forward, if such an animation is running.
    //
    // TODO: We overdo damage, for ex. in the following cases:
    // - Expo does not need any damage (can't really be fixed, since we don't know
    // the plugin which uses this API).
    // - If the view has not updated, and cursor has not moved
    effect_hook_t damage_overlay = [=] ()
    {
        apply_damage();
    };

    effect_hook_t render_overlay = [=] ()
    {
        auto fb = output->render->get_target_framebuffer();
        fb.geometry = output->get_layout_geometry();

        // FIXME: this is not accurate, but it kinda works ..
        // It should be fixed once the dragged view is an actual node, then the
        // output will be responsible for sending it frame events.
        timespec repaint_ended;
        clockid_t presentation_clock =
            wlr_backend_get_presentation_clock(wf::get_core().backend);
        clock_gettime(presentation_clock, &repaint_ended);

        for (auto& view : wf::reverse(views))
        {
            // Convert damage from output-local coordinates (last_bbox) to
            // output-layout coords.
            wf::region_t damage;
            damage |= view.last_bbox + wf::origin(fb.geometry);

            // Render the full view, always
            // Not very efficient
            view.view->render_transformed(fb, std::move(damage));
            for (auto& surf : view.view->enumerate_surfaces())
            {
                surf.surface->send_frame_done(repaint_ended);
            }
        }
    };
};

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
class core_drag_t : public signal_provider_t
{
    /**
     * Rebuild the wobbly model after a change in the scaling, so that the wobbly
     * model does not try to animate the scaling change itself.
     */
    void rebuild_wobbly(wayfire_view view, wf::point_t grab, wf::pointf_t relative)
    {
        auto dim = wf::dimensions(view->get_bounding_box("wobbly"));
        modify_wobbly(view, find_geometry_around(dim, grab, relative));
    }

  public:
    /**
     * Start drag.
     *
     * @param grab_view The view which is being dragged.
     * @param grab_position The position of the input, in output-layout coordinates.
     * @param relative The position of the grab_position relative to view.
     */
    void start_drag(wayfire_view grab_view, wf::point_t grab_position,
        wf::pointf_t relative,
        const drag_options_t& options)
    {
        auto bbox = grab_view->get_bounding_box("wobbly");
        wf::point_t rel_grab_pos = {
            int(bbox.x + relative.x * bbox.width),
            int(bbox.y + relative.y * bbox.height),
        };

        if (options.join_views)
        {
            grab_view = get_toplevel(grab_view);
        }

        this->view   = grab_view;
        this->params = options;

        auto target_views = get_target_views(grab_view, options.join_views);
        for (auto& v : target_views)
        {
            dragged_view_t dragged;
            dragged.view = v;

            // Setup view transform
            auto tr = std::make_unique<scale_around_grab_t>();
            dragged.transformer = {tr};

            tr->relative_grab = find_relative_grab(
                v->get_bounding_box("wobbly"), rel_grab_pos);
            tr->grab_position = grab_position;
            tr->scale_factor.animate(options.initial_scale, options.initial_scale);

            v->add_transformer(std::move(tr), move_drag_transformer);

            // Hide the view, we will render it as an overlay
            wf::scene::set_node_enabled(v->get_main_node(), false);
            v->damage();

            // Make sure that wobbly has the correct geometry from the start!
            rebuild_wobbly(v, grab_position, dragged.transformer->relative_grab);

            // TODO: make this configurable!
            start_wobbly_rel(v, dragged.transformer->relative_grab);

            this->all_views.push_back(dragged);
            v->connect_signal("unmapped", &on_view_unmap);
        }

        // Setup overlay hooks
        for (auto& output : wf::get_core().output_layout->get_outputs())
        {
            output->store_data(
                std::make_unique<output_data_t>(output, all_views));
        }

        wf::get_core().set_cursor("grabbing");

        // Set up snap-off
        if (params.enable_snap_off)
        {
            for (auto& v : all_views)
            {
                set_tiled_wobbly(v.view, true);
            }

            grab_origin = grab_position;
            view_held_in_place = true;
        }
    }

    void start_drag(wayfire_view view, wf::point_t grab_position,
        const drag_options_t& options)
    {
        if (options.join_views)
        {
            view = get_toplevel(view);
        }

        auto bbox = view->get_bounding_box() +
            wf::origin(view->get_output()->get_layout_geometry());
        start_drag(view, grab_position,
            find_relative_grab(bbox, grab_position), options);
    }

    void handle_motion(wf::point_t to)
    {
        if (view_held_in_place)
        {
            if (distance_to_grab_origin(to) >= (double)params.snap_off_threshold)
            {
                view_held_in_place = false;
                for (auto& v : all_views)
                {
                    set_tiled_wobbly(v.view, false);
                }

                snap_off_signal data;
                data.focus_output = current_output;
                emit_signal("snap-off", &data);
            }
        }

        // Update wobbly independently of the grab position.
        // This is because while held in place, wobbly is anchored to its edges
        // so we can still move the grabbed point without moving the view.
        for (auto& v : all_views)
        {
            move_wobbly(v.view, to.x, to.y);
            if (!view_held_in_place)
            {
                v.transformer->grab_position = to;
            }
        }

        update_current_output(to);
    }

    double distance_to_grab_origin(wf::point_t to) const
    {
        auto offset = to - grab_origin;
        const int dst_sq = offset.x * offset.x + offset.y * offset.y;
        return std::sqrt(dst_sq);
    }

    void handle_input_released()
    {
        // Store data for the drag done signal
        drag_done_signal data;
        data.grab_position = all_views.front().transformer->grab_position;
        for (auto& v : all_views)
        {
            data.all_views.push_back(
                {v.view, v.transformer->relative_grab});
        }

        data.main_view = this->view;
        data.focused_output = current_output;
        data.join_views     = params.join_views;

        // Remove overlay hooks and damage outputs BEFORE popping the transformer
        for (auto& output : wf::get_core().output_layout->get_outputs())
        {
            output->get_data<output_data_t>()->apply_damage();
            output->erase_data<output_data_t>();
        }

        for (auto& v : all_views)
        {
            auto grab_position = v.transformer->grab_position;
            auto rel_pos = v.transformer->relative_grab;

            // Restore view to where it was before
            wf::scene::set_node_enabled(v.view->get_main_node(), true);
            v.view->pop_transformer(move_drag_transformer);

            // Reset wobbly and leave it in output-LOCAL coordinates
            end_wobbly(v.view);

            // Important! If the view scale was not 1.0, the wobbly model needs to be
            // updated with the new size. Since this is an artificial resize, we need
            // to make sure that the resize happens smoothly.
            rebuild_wobbly(v.view, grab_position, rel_pos);

            // Put wobbly back in output-local space, the plugins will take it from
            // here.
            translate_wobbly(v.view,
                -wf::origin(v.view->get_output()->get_layout_geometry()));
        }

        // Reset our state
        view = nullptr;
        all_views.clear();
        current_output     = nullptr;
        view_held_in_place = false;
        wf::get_core().set_cursor("default");

        // Lastly, let the plugins handle what happens on drag end.
        emit_signal("done", &data);
        on_view_unmap.disconnect();
    }

    void set_scale(double new_scale)
    {
        for (auto& view : all_views)
        {
            view.transformer->scale_factor.animate(new_scale);
        }
    }

    bool is_view_held_in_place()
    {
        return view_held_in_place;
    }

    // View currently being moved.
    wayfire_view view;

    // Output where the action is happening.
    wf::output_t *current_output = NULL;

  private:
    // All views being dragged, more than one in case of join_views.
    std::vector<dragged_view_t> all_views;

    // Current parameters
    drag_options_t params;

    // Grab origin, used for snap-off
    wf::point_t grab_origin;

    // View is held in place, waiting for snap-off
    bool view_held_in_place = false;

    void update_current_output(wf::point_t grab)
    {
        wf::pointf_t origin = {1.0 * grab.x, 1.0 * grab.y};
        auto output =
            wf::get_core().output_layout->get_output_coords_at(origin, origin);

        if (output != current_output)
        {
            drag_focus_output_signal data;
            data.previous_focus_output = current_output;

            current_output    = output;
            data.focus_output = output;
            wf::get_core().focus_output(output);
            emit_signal("focus-output", &data);
        }
    }

    wf::signal_connection_t on_view_unmap = [=] (auto *ev)
    {
        handle_input_released();
    };
};

/**
 * Move the view to the target output and put it at the coordinates of the grab.
 * Also take into account view's fullscreen and tiled state.
 *
 * Unmapped views are ignored.
 */
inline void adjust_view_on_output(drag_done_signal *ev)
{
    // Any one of the views that are being dragged.
    // They are all part of the same view tree.
    auto parent = get_toplevel(ev->main_view);
    if (!parent->is_mapped())
    {
        return;
    }

    if (parent->get_output() != ev->focused_output)
    {
        wf::get_core().move_view_to_output(parent, ev->focused_output, false);
    }

    // Calculate the position we're leaving the view on
    auto output_delta = -wf::origin(ev->focused_output->get_layout_geometry());
    auto grab = ev->grab_position + output_delta;

    auto output_geometry = ev->focused_output->get_relative_geometry();
    auto current_ws = ev->focused_output->workspace->get_current_workspace();
    wf::point_t target_ws{
        (int)std::floor(1.0 * grab.x / output_geometry.width),
        (int)std::floor(1.0 * grab.y / output_geometry.height),
    };
    target_ws = target_ws + current_ws;

    auto gsize = ev->focused_output->workspace->get_workspace_grid_size();
    target_ws.x = wf::clamp(target_ws.x, 0, gsize.width - 1);
    target_ws.y = wf::clamp(target_ws.y, 0, gsize.height - 1);

    // view to focus at the end of drag
    auto focus_view = ev->main_view;

    for (auto& v : ev->all_views)
    {
        if (!v.view->is_mapped())
        {
            // Maybe some dialog got unmapped
            continue;
        }

        auto bbox = v.view->get_bounding_box("wobbly");
        auto wm   = v.view->get_wm_geometry();

        wf::point_t wm_offset = wf::origin(wm) + -wf::origin(bbox);
        bbox = wf::move_drag::find_geometry_around(
            wf::dimensions(bbox), grab, v.relative_grab);

        wf::point_t target = wf::origin(bbox) + wm_offset;
        v.view->move(target.x, target.y);
        if (v.view->fullscreen)
        {
            v.view->fullscreen_request(ev->focused_output, true, target_ws);
        } else if (v.view->tiled_edges)
        {
            v.view->tile_request(v.view->tiled_edges, target_ws);
        }

        // check focus timestamp and select the last focused view to (re)focus
        if (v.view->last_focus_timestamp > focus_view->last_focus_timestamp)
        {
            focus_view = v.view;
        }
    }

    // Ensure that every view is visible on parent's main workspace
    for (auto& v : parent->enumerate_views())
    {
        ev->focused_output->workspace->move_to_workspace(v, target_ws);
    }

    ev->focused_output->focus_view(focus_view, true);
}

/**
 * Adjust the view's state after snap-off.
 */
inline void adjust_view_on_snap_off(wayfire_view view)
{
    if (view->tiled_edges && !view->fullscreen)
    {
        view->tile_request(0);
    }
}
}
}
