#include "wayfire/plugins/common/move-drag-interface.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/seat.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/util/log.hpp>
#include <cmath>
#include <wayfire/view-transform.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/nonstd/reverse.hpp>
#include <wayfire/plugins/common/util.hpp>
#include <wayfire/plugins/wobbly/wobbly-signal.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/workspace-set.hpp>
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/view-helpers.hpp"

namespace wf
{
namespace move_drag
{
static wf::geometry_t find_geometry_around(wf::dimensions_t size, wf::point_t grab, wf::pointf_t relative)
{
    return wf::geometry_t{
        grab.x - (int)std::floor(relative.x * size.width),
        grab.y - (int)std::floor(relative.y * size.height),
        size.width,
        size.height,
    };
}

/**
 * A transformer used while dragging.
 *
 * It is primarily used to scale the view is a plugin needs it, and also to keep it
 * centered around the `grab_position`.
 */
class scale_around_grab_t : public wf::scene::transformer_base_node_t
{
  public:
    /**
     * Factor for scaling down the view.
     * A factor 2.0 means that the view will have half of its width and height.
     */
    wf::animation::simple_animation_t scale_factor{wf::create_option(300)};

    wf::animation::simple_animation_t alpha_factor{wf::create_option(300)};

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

    scale_around_grab_t() : transformer_base_node_t(false)
    {}

    std::string stringify() const override
    {
        return "move-drag";
    }

    wf::pointf_t scale_around_grab(wf::pointf_t point, double factor)
    {
        auto bbox = get_children_bounding_box();
        auto gx   = bbox.x + bbox.width * relative_grab.x;
        auto gy   = bbox.y + bbox.height * relative_grab.y;

        return {
            (point.x - gx) * factor + gx,
            (point.y - gy) * factor + gy,
        };
    }

    wf::pointf_t to_local(const wf::pointf_t& point) override
    {
        return scale_around_grab(point, scale_factor);
    }

    wf::pointf_t to_global(const wf::pointf_t& point) override
    {
        return scale_around_grab(point, 1.0 / scale_factor);
    }

    wf::geometry_t get_bounding_box() override
    {
        auto bbox = get_children_bounding_box();
        int w     = std::floor(bbox.width / scale_factor);
        int h     = std::floor(bbox.height / scale_factor);
        return find_geometry_around({w, h}, grab_position, relative_grab);
    }

    class render_instance_t :
        public scene::transformer_render_instance_t<scale_around_grab_t>
    {
      public:
        using transformer_render_instance_t::transformer_render_instance_t;

        void transform_damage_region(wf::region_t& region) override
        {
            region |= self->get_bounding_box();
        }

        void render(const wf::render_target_t& target,
            const wf::region_t& region) override
        {
            auto bbox = self->get_bounding_box();
            auto tex  = this->get_texture(target.scale);

            OpenGL::render_begin(target);
            for (auto& rect : region)
            {
                target.logic_scissor(wlr_box_from_pixman_box(rect));
                OpenGL::render_texture(tex, target, bbox, glm::vec4{1, 1, 1, (double)self->alpha_factor});
            }

            OpenGL::render_end();
        }
    };

    void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
        scene::damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<render_instance_t>(this,
            push_damage, shown_on));
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
    wayfire_toplevel_view view;

    // Its transformer
    std::shared_ptr<scale_around_grab_t> transformer;

    // The last bounding box used for damage.
    // This is needed in case the view resizes or something like that, in which
    // case we don't have access to the previous bbox.
    wf::geometry_t last_bbox;
};

// A node to render the dragged views in global coordinates.
// The assumption is that all nodes have a view transformer which transforms them to global (not output-local)
// coordinates and thus we just need to schedule them for rendering.
class dragged_view_node_t : public wf::scene::node_t
{
  public:
    std::vector<dragged_view_t> views;
    dragged_view_node_t(std::vector<dragged_view_t> views) : node_t(false)
    {
        this->views = views;
    }

    std::string stringify() const override
    {
        return "move-drag-view " + stringify_flags();
    }

    void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
        scene::damage_callback push_damage, wf::output_t *output = nullptr) override
    {
        instances.push_back(std::make_unique<dragged_view_render_instance_t>(
            std::dynamic_pointer_cast<dragged_view_node_t>(shared_from_this()), push_damage, output));
    }

    wf::geometry_t get_bounding_box() override
    {
        wf::region_t bounding;
        for (auto& view : views)
        {
            // Note: bbox will be in output layout coordinates now, since this is
            // how the transformer works
            auto bbox = view.view->get_transformed_node()->get_bounding_box();
            bounding |= bbox;
        }

        return wlr_box_from_pixman_box(bounding.get_extents());
    }

    class dragged_view_render_instance_t : public wf::scene::render_instance_t
    {
        wf::geometry_t last_bbox = {0, 0, 0, 0};
        wf::scene::damage_callback push_damage;
        std::vector<scene::render_instance_uptr> children;
        wf::signal::connection_t<scene::node_damage_signal> on_node_damage =
            [=] (scene::node_damage_signal *data)
        {
            push_damage(data->region);
        };

      public:
        dragged_view_render_instance_t(std::shared_ptr<dragged_view_node_t> self,
            wf::scene::damage_callback push_damage, wf::output_t *shown_on)
        {
            auto push_damage_child = [=] (wf::region_t child_damage)
            {
                push_damage(last_bbox);
                last_bbox = self->get_bounding_box();
                push_damage(last_bbox);
            };

            for (auto& view : self->views)
            {
                auto node = view.view->get_transformed_node();
                node->gen_render_instances(children, push_damage_child, shown_on);
            }
        }

        void schedule_instructions(std::vector<scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::region_t& damage) override
        {
            for (auto& inst : children)
            {
                inst->schedule_instructions(instructions, target, damage);
            }
        }

        void presentation_feedback(wf::output_t *output) override
        {
            for (auto& instance : children)
            {
                instance->presentation_feedback(output);
            }
        }

        void compute_visibility(wf::output_t *output, wf::region_t& visible) override
        {
            for (auto& instance : children)
            {
                const int BIG_NUMBER    = 1e5;
                wf::region_t big_region =
                    wf::geometry_t{-BIG_NUMBER, -BIG_NUMBER, 2 * BIG_NUMBER, 2 * BIG_NUMBER};
                instance->compute_visibility(output, big_region);
            }
        }
    };
};

struct core_drag_t::impl
{
    // All views being dragged, more than one in case of join_views.
    std::vector<dragged_view_t> all_views;

    // Current parameters
    drag_options_t params;

    // View is held in place, waiting for snap-off
    bool view_held_in_place = false;

    std::shared_ptr<dragged_view_node_t> render_node;

    wf::effect_hook_t on_pre_frame = [=] ()
    {
        for (auto& v : this->all_views)
        {
            if (v.transformer->scale_factor.running())
            {
                v.view->damage();
            }
        }
    };

    wf::signal::connection_t<view_unmapped_signal> on_view_unmap;
    wf::signal::connection_t<output_removed_signal> on_output_removed;
};

core_drag_t::core_drag_t()
{
    this->priv = std::make_unique<impl>();

    priv->on_view_unmap = [=] (auto *ev)
    {
        handle_input_released();
    };

    priv->on_output_removed = [=] (wf::output_removed_signal *ev)
    {
        if (current_output == ev->output)
        {
            update_current_output(nullptr);
        }
    };

    wf::get_core().output_layout->connect(&priv->on_output_removed);
}

core_drag_t::~core_drag_t() = default;

void core_drag_t::rebuild_wobbly(wayfire_toplevel_view view, wf::point_t grab, wf::pointf_t relative)
{
    auto dim = wf::dimensions(wf::view_bounding_box_up_to(view, "wobbly"));
    modify_wobbly(view, find_geometry_around(dim, grab, relative));
}

bool core_drag_t::should_start_pending_drag(wf::point_t current_position)
{
    if (!tentative_grab_position.has_value())
    {
        return false;
    }

    return distance_to_grab_origin(current_position) > 5;
}

void core_drag_t::start_drag(wayfire_toplevel_view grab_view, wf::pointf_t relative,
    const drag_options_t& options)
{
    wf::dassert(tentative_grab_position.has_value(),
        "First, the drag operation should be set as pending!");
    wf::dassert(grab_view->is_mapped(), "Dragged view should be mapped!");
    wf::dassert(!this->view, "Drag operation already in progress!");

    auto bbox = wf::view_bounding_box_up_to(grab_view, "wobbly");
    wf::point_t rel_grab_pos = {
        int(bbox.x + relative.x * bbox.width),
        int(bbox.y + relative.y * bbox.height),
    };

    if (options.join_views)
    {
        grab_view = wf::find_topmost_parent(grab_view);
    }

    this->view   = grab_view;
    priv->params = options;
    wf::get_core().default_wm->set_view_grabbed(view, true);

    auto target_views = get_target_views(grab_view, options.join_views);
    for (auto& v : target_views)
    {
        dragged_view_t dragged;
        dragged.view = v;

        // Setup view transform

        auto tr = std::make_shared<scale_around_grab_t>();
        dragged.transformer = {tr};

        tr->relative_grab = find_relative_grab(
            wf::view_bounding_box_up_to(v, "wobbly"), rel_grab_pos);
        tr->grab_position = *tentative_grab_position;
        tr->scale_factor.animate(options.initial_scale, options.initial_scale);
        tr->alpha_factor.animate(1, 1);
        v->get_transformed_node()->add_transformer(
            tr, wf::TRANSFORMER_HIGHLEVEL - 1);

        // Hide the view, we will render it as an overlay
        wf::scene::set_node_enabled(v->get_transformed_node(), false);
        v->damage();

        // Make sure that wobbly has the correct geometry from the start!
        rebuild_wobbly(v, *tentative_grab_position, dragged.transformer->relative_grab);

        // TODO: make this configurable!
        start_wobbly_rel(v, dragged.transformer->relative_grab);

        priv->all_views.push_back(dragged);
        v->connect(&priv->on_view_unmap);
    }

    // Setup overlay hooks
    priv->render_node = std::make_shared<dragged_view_node_t>(priv->all_views);
    wf::scene::add_front(wf::get_core().scene(), priv->render_node);
    wf::get_core().set_cursor("grabbing");

    // Set up snap-off
    if (priv->params.enable_snap_off)
    {
        for (auto& v : priv->all_views)
        {
            set_tiled_wobbly(v.view, true);
        }

        priv->view_held_in_place = true;
    }
}

void core_drag_t::start_drag(wayfire_toplevel_view view, const drag_options_t& options)
{
    wf::dassert(tentative_grab_position.has_value(),
        "First, the drag operation should be set as pending!");

    if (options.join_views)
    {
        view = wf::find_topmost_parent(view);
    }

    auto bbox = view->get_transformed_node()->get_bounding_box() +
        wf::origin(view->get_output()->get_layout_geometry());
    start_drag(view, find_relative_grab(bbox, *tentative_grab_position), options);
}

void core_drag_t::handle_motion(wf::point_t to)
{
    if (priv->view_held_in_place)
    {
        if (distance_to_grab_origin(to) >= (double)priv->params.snap_off_threshold)
        {
            priv->view_held_in_place = false;
            for (auto& v : priv->all_views)
            {
                set_tiled_wobbly(v.view, false);
            }

            snap_off_signal data;
            data.focus_output = current_output;
            emit(&data);
        }
    }

    // Update wobbly independently of the grab position.
    // This is because while held in place, wobbly is anchored to its edges
    // so we can still move the grabbed point without moving the view.
    for (auto& v : priv->all_views)
    {
        move_wobbly(v.view, to.x, to.y);
        if (!priv->view_held_in_place)
        {
            v.view->get_transformed_node()->begin_transform_update();
            v.transformer->grab_position = to;
            v.view->get_transformed_node()->end_transform_update();
        }
    }

    update_current_output(to);

    drag_motion_signal data;
    data.current_position = to;
    emit(&data);
}

double core_drag_t::distance_to_grab_origin(wf::point_t to) const
{
    return abs(to - *tentative_grab_position);
}

void core_drag_t::handle_input_released()
{
    if (!view || priv->all_views.empty())
    {
        this->tentative_grab_position = {};
        // Input already released => don't do anything
        return;
    }

    // Store data for the drag done signal
    drag_done_signal data;
    data.grab_position = priv->all_views.front().transformer->grab_position;
    for (auto& v : priv->all_views)
    {
        data.all_views.push_back(
            {v.view, v.transformer->relative_grab});
    }

    data.main_view = this->view;
    data.focused_output = current_output;
    data.join_views     = priv->params.join_views;

    // Remove overlay hooks and damage outputs BEFORE popping the transformer
    wf::scene::remove_child(priv->render_node);
    priv->render_node->views.clear();
    priv->render_node = nullptr;

    for (auto& v : priv->all_views)
    {
        auto grab_position = v.transformer->grab_position;
        auto rel_pos = v.transformer->relative_grab;

        // Restore view to where it was before
        wf::scene::set_node_enabled(v.view->get_transformed_node(), true);
        v.view->get_transformed_node()->rem_transformer<scale_around_grab_t>();

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
    wf::get_core().default_wm->set_view_grabbed(view, false);
    view = nullptr;
    priv->all_views.clear();
    if (current_output)
    {
        current_output->render->rem_effect(&priv->on_pre_frame);
        current_output = nullptr;
    }

    wf::get_core().set_cursor("default");

    // Lastly, let the plugins handle what happens on drag end.
    emit(&data);
    priv->view_held_in_place = false;
    priv->on_view_unmap.disconnect();

    this->tentative_grab_position = {};
}

void core_drag_t::set_scale(double new_scale, double alpha)
{
    for (auto& view : priv->all_views)
    {
        view.transformer->scale_factor.animate(new_scale);
        view.transformer->alpha_factor.animate(alpha);
    }
}

bool core_drag_t::is_view_held_in_place()
{
    return priv->view_held_in_place;
}

void core_drag_t::update_current_output(wf::point_t grab)
{
    wf::pointf_t origin = {1.0 * grab.x, 1.0 * grab.y};
    auto output = wf::get_core().output_layout->get_output_coords_at(origin, origin);
    update_current_output(output);
}

void core_drag_t::update_current_output(wf::output_t *output)
{
    if (output != current_output)
    {
        if (current_output)
        {
            current_output->render->rem_effect(&priv->on_pre_frame);
        }

        drag_focus_output_signal data;
        data.previous_focus_output = current_output;
        current_output    = output;
        data.focus_output = output;
        if (output)
        {
            wf::get_core().seat->focus_output(output);
            output->render->add_effect(&priv->on_pre_frame, OUTPUT_EFFECT_PRE);
        }

        emit(&data);
    }
}

/**
 * Move the view to the target output and put it at the coordinates of the grab.
 * Also take into account view's fullscreen and tiled state.
 *
 * Unmapped views are ignored.
 */
void adjust_view_on_output(drag_done_signal *ev)
{
    // Any one of the views that are being dragged.
    // They are all part of the same view tree.
    auto parent = wf::find_topmost_parent(ev->main_view);
    if (!parent->is_mapped())
    {
        return;
    }

    const bool change_output = parent->get_output() != ev->focused_output;
    auto old_wset = parent->get_wset();
    if (change_output)
    {
        start_move_view_to_wset(parent, ev->focused_output->wset());
    }

    // Calculate the position we're leaving the view on
    auto output_delta = -wf::origin(ev->focused_output->get_layout_geometry());
    auto grab = ev->grab_position + output_delta;

    auto output_geometry = ev->focused_output->get_relative_geometry();
    auto current_ws = ev->focused_output->wset()->get_current_workspace();
    wf::point_t target_ws{
        (int)std::floor(1.0 * grab.x / output_geometry.width),
        (int)std::floor(1.0 * grab.y / output_geometry.height),
    };
    target_ws = target_ws + current_ws;

    auto gsize = ev->focused_output->wset()->get_workspace_grid_size();
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

        auto bbox = wf::view_bounding_box_up_to(v.view, "wobbly");
        auto wm   = v.view->get_geometry();

        wf::point_t wm_offset = wf::origin(wm) + -wf::origin(bbox);
        bbox = wf::move_drag::find_geometry_around(
            wf::dimensions(bbox), grab, v.relative_grab);

        wf::point_t target = wf::origin(bbox) + wm_offset;
        v.view->move(target.x, target.y);
        if (v.view->pending_fullscreen())
        {
            wf::get_core().default_wm->fullscreen_request(v.view, ev->focused_output, true, target_ws);
        } else if (v.view->pending_tiled_edges())
        {
            wf::get_core().default_wm->tile_request(v.view, v.view->pending_tiled_edges(), target_ws);
        }

        // check focus timestamp and select the last focused view to (re)focus
        if (get_focus_timestamp(v.view) > get_focus_timestamp(focus_view))
        {
            focus_view = v.view;
        }
    }

    // Ensure that every view is visible on parent's main workspace
    for (auto& v : parent->enumerate_views())
    {
        ev->focused_output->wset()->move_to_workspace(v, target_ws);
    }

    if (change_output)
    {
        emit_view_moved_to_wset(parent, old_wset, ev->focused_output->wset());
    }

    wf::get_core().default_wm->focus_raise_view(focus_view);
}

/**
 * Adjust the view's state after snap-off.
 */
void adjust_view_on_snap_off(wayfire_toplevel_view view)
{
    if (view->pending_tiled_edges() && !view->pending_fullscreen())
    {
        wf::get_core().default_wm->tile_request(view, 0);
    }
}
}
}
