#pragma once

#include "tree-controller.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/plugins/common/move-drag-interface.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include "wayfire/plugins/common/preview-indication.hpp"
#include "tree.hpp"
#include "tile-wset.hpp"
#include "wayfire/debug.hpp"

namespace wf
{
namespace tile
{
class drag_manager_t
{
    wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> drag_helper;
    std::shared_ptr<wf::preview_indication_t> preview;
    bool currently_dropping = false;

  public:
    drag_manager_t()
    {
        drag_helper->connect(&on_drag_motion);
        drag_helper->connect(&on_drag_output_focus);
        drag_helper->connect(&on_drag_done);
    }

    ~drag_manager_t()
    {
        hide_preview();
    }

    wf::signal::connection_t<wf::move_drag::drag_motion_signal> on_drag_motion =
        [=] (wf::move_drag::drag_motion_signal *ev)
    {
        if (should_show_preview(drag_helper->view, drag_helper->current_output))
        {
            update_preview(drag_helper->current_output, drag_helper->view);
        }
    };

    wf::signal::connection_t<wf::move_drag::drag_focus_output_signal> on_drag_output_focus =
        [=] (wf::move_drag::drag_focus_output_signal *ev)
    {
        if (should_show_preview(drag_helper->view, ev->focus_output))
        {
            drag_helper->set_scale(2, 0.5);
            update_preview(ev->focus_output, drag_helper->view);
        }
    };

    wf::signal::connection_t<wf::move_drag::drag_done_signal> on_drag_done =
        [=] (wf::move_drag::drag_done_signal *ev)
    {
        if (should_show_preview(ev->main_view, ev->focused_output))
        {
            currently_dropping = true;
            if (!handle_drop(ev->main_view, ev->focused_output))
            {
                if (ev->main_view->get_output() != ev->focused_output)
                {
                    // Allow usual movement to the new output.
                    currently_dropping = false;
                    move_drag::adjust_view_on_output(ev);
                }
            }

            currently_dropping = false;
        }

        hide_preview();
    };

    bool is_dragging(wayfire_toplevel_view view)
    {
        return currently_dropping;
    }

    bool dragged_view_is_tiled(wayfire_toplevel_view view)
    {
        return view && view_node_t::get_node(view);
    }

    bool should_show_preview(wayfire_toplevel_view view, wf::output_t *output)
    {
        return dragged_view_is_tiled(view) && output &&
               (output->can_activate_plugin(CAPABILITY_MANAGE_COMPOSITOR) ||
                   output->is_plugin_active("simple-tile"));
    }

    void hide_preview()
    {
        if (this->preview)
        {
            auto target = preview->get_output() ?
                preview->get_output()->get_cursor_position().round_down() : wf::point_t{0, 0};
            this->preview->set_target_geometry(target, 0.0, true);
            this->preview.reset();
        }
    }

    void update_preview(wf::output_t *output, wayfire_toplevel_view dragged_view)
    {
        auto input = get_global_input_coordinates(output);
        auto view  = check_drop_destination(output, input, dragged_view);
        if (!view)
        {
            return hide_preview();
        }

        auto split = calculate_insert_type(view, input);
        if (preview && (preview->get_output() != output))
        {
            hide_preview();
        }

        if (!this->preview)
        {
            auto start_coords = get_wset_local_coordinates(output->wset(), input);
            preview = std::make_shared<wf::preview_indication_t>(start_coords, output, "simple-tile");
        }

        auto preview_geometry = calculate_split_preview(view, split);
        preview_geometry = get_wset_local_coordinates(output->wset(), preview_geometry);
        if (preview_geometry != preview->get_target_geometry())
        {
            this->preview->set_target_geometry(preview_geometry, 1.0);
        }
    }

    static int find_idx(nonstd::observer_ptr<tree_node_t> child)
    {
        auto& children = child->parent->children;
        auto it = std::find_if(children.begin(), children.end(),
            [child] (const std::unique_ptr<tree_node_t>& n) { return n.get() == child.get(); });

        wf::dassert(it != children.end(), "Child not found");
        return it - children.begin();
    }

    static int remove_child(nonstd::observer_ptr<tree_node_t> child)
    {
        int idx = find_idx(child);
        child->parent->children.erase(child->parent->children.begin() + idx);
        return idx;
    }

    void move_tiled_view(wayfire_toplevel_view view, wf::output_t *target)
    {
        wf::scene::remove_child(view->get_root_node());
        view->get_wset()->remove_view(view);
        target->wset()->add_view(view);
        auto& wsdata = tile_workspace_set_data_t::get(target);
        auto vp = target->wset()->get_current_workspace();
        wf::scene::readd_front(wsdata.tiled_sublayer[vp.x][vp.y], view->get_root_node());
    }

    void handle_swap(wayfire_toplevel_view a, wayfire_toplevel_view b)
    {
        auto output_a = a->get_output();
        auto output_b = b->get_output();

        if (output_a != output_b)
        {
            wf::emit_view_pre_moved_to_wset_pre(a, output_a->wset(), output_b->wset());
            wf::emit_view_pre_moved_to_wset_pre(b, output_b->wset(), output_a->wset());
            move_tiled_view(a, output_b);
            move_tiled_view(b, output_a);
        }

        {
            autocommit_transaction_t tx;

            auto old_tile_a = view_node_t::get_node(a);
            auto old_tile_b = view_node_t::get_node(b);
            auto parent_a   = old_tile_a->parent;
            auto parent_b   = old_tile_b->parent;

            auto geometry_a = old_tile_a->geometry;
            auto geometry_b = old_tile_b->geometry;
            auto gaps_a     = old_tile_a->get_gaps();
            auto gaps_b     = old_tile_b->get_gaps();
            int idx_a = remove_child(old_tile_a);
            int idx_b = remove_child(old_tile_b);

            auto new_b = std::make_unique<tile::view_node_t>(b);
            new_b->set_gaps(gaps_a);
            new_b->set_geometry(geometry_a, tx.tx);

            auto new_a = std::make_unique<tile::view_node_t>(a);
            new_a->set_gaps(gaps_b);
            new_a->set_geometry(geometry_b, tx.tx);

            new_a->parent = parent_b;
            new_b->parent = parent_a;

            // Insert to the smaller position first, if they both have the same parent.
            // Otherwise the larger position might not be valid because we removed 2 elements.
            if ((parent_a != parent_b) || (idx_a < idx_b))
            {
                parent_a->children.insert(parent_a->children.begin() + idx_a, std::move(new_b));
                parent_b->children.insert(parent_b->children.begin() + idx_b, std::move(new_a));
            } else
            {
                parent_b->children.insert(parent_b->children.begin() + idx_b, std::move(new_a));
                parent_a->children.insert(parent_a->children.begin() + idx_a, std::move(new_b));
            }
        }

        if (output_a != output_b)
        {
            wf::emit_view_moved_to_wset(a, output_a->wset(), output_b->wset());
            wf::emit_view_moved_to_wset(b, output_b->wset(), output_a->wset());
        }
    }

    void handle_move_retile(wayfire_toplevel_view source, nonstd::observer_ptr<tile::view_node_t> target,
        split_insertion_t split)
    {
        auto source_output = source->get_output();
        auto target_output = target->view->get_output();

        if (source_output != target_output)
        {
            wf::emit_view_pre_moved_to_wset_pre(source, source->get_wset(), target->view->get_wset());
            move_tiled_view(source, target_output);
        }

        autocommit_transaction_t tx;
        auto split_type = (split == INSERT_LEFT || split == INSERT_RIGHT) ?
            SPLIT_VERTICAL : SPLIT_HORIZONTAL;

        auto source_node = view_node_t::get_node(source);
        if (target->parent->get_split_direction() == split_type)
        {
            /* We can simply add the dragged view as a sibling of the target view */
            auto src = source_node->parent->remove_child(source_node, tx.tx);

            int idx = find_idx(target);
            if ((split == INSERT_RIGHT) || (split == INSERT_BELOW))
            {
                ++idx;
            }

            target->parent->add_child(std::move(src), tx.tx, idx);
        } else
        {
            /* Case 2: we need a new split just for the dropped on and the dragged
             * views */
            auto new_split = std::make_unique<split_node_t>(split_type);
            /* The size will be autodetermined by the tree structure, but we set
             * some valid size here to avoid UB */
            new_split->set_geometry(target->geometry, tx.tx);

            /* Find the position of the dropped view and its parent */
            int idx = find_idx(target);
            auto dropped_parent = target->parent;

            /* Remove both views */
            auto dropped_view = target->parent->remove_child(target, tx.tx);
            auto dragged_view = source_node->parent->remove_child(source_node, tx.tx);

            if ((split == INSERT_ABOVE) || (split == INSERT_LEFT))
            {
                new_split->add_child(std::move(dragged_view), tx.tx);
                new_split->add_child(std::move(dropped_view), tx.tx);
            } else
            {
                new_split->add_child(std::move(dropped_view), tx.tx);
                new_split->add_child(std::move(dragged_view), tx.tx);
            }

            /* Put them in place */
            dropped_parent->add_child(std::move(new_split), tx.tx, idx);
        }

        tile_workspace_set_data_t::get(source_output).refresh(tx.tx);
        tile_workspace_set_data_t::get(target_output).refresh(tx.tx);

        if (source_output != target_output)
        {
            wf::emit_view_moved_to_wset(source, source_output->wset(), target_output->wset());
        }
    }

    bool handle_drop(wayfire_toplevel_view view, wf::output_t *output)
    {
        auto input = get_global_input_coordinates(output);
        auto dropped_at = check_drop_destination(output, input, view);
        if (!dropped_at)
        {
            return false;
        }

        auto split = calculate_insert_type(dropped_at, input);
        if (split == INSERT_NONE)
        {
            return false;
        }

        if (split == INSERT_SWAP)
        {
            handle_swap(view, dropped_at->view);
        } else
        {
            handle_move_retile(view, dropped_at, split);
        }

        return true;
    }

    /**
     * Calculate the position of the split that needs to be created if a view is
     * dropped at @input over @node
     *
     * @param sensitivity What percentage of the view is "active", i.e the threshold
     *                    for INSERT_NONE
     */
    static split_insertion_t calculate_insert_type(
        nonstd::observer_ptr<tree_node_t> node, wf::point_t input, double sensitivity)
    {
        auto window = node->geometry;

        if (!(window & input))
        {
            return INSERT_NONE;
        }

        /*
         * Calculate how much to the left, right, top and bottom of the window
         * our input is, then filter through the sensitivity.
         *
         * In the end, take the edge which is closest to input.
         */
        std::vector<std::pair<double, split_insertion_t>> edges;

        double px = 1.0 * (input.x - window.x) / window.width;
        double py = 1.0 * (input.y - window.y) / window.height;

        edges.push_back({px, INSERT_LEFT});
        edges.push_back({py, INSERT_ABOVE});
        edges.push_back({1.0 - px, INSERT_RIGHT});
        edges.push_back({1.0 - py, INSERT_BELOW});

        /* Remove edges that are too far away */
        auto it = std::remove_if(edges.begin(), edges.end(),
            [sensitivity] (auto pair)
        {
            return pair.first > sensitivity;
        });
        edges.erase(it, edges.end());

        if (edges.empty())
        {
            return INSERT_SWAP;
        }

        /* Return the closest edge */
        return std::min_element(edges.begin(), edges.end())->second;
    }

    /* By default, 1/3rd of the view can be dropped into */
    static constexpr double SPLIT_PREVIEW_PERCENTAGE = 1.0 / 3.0;

    /**
     * Calculate the position of the split that needs to be created if a view is
     * dropped at @input over @node
     */
    split_insertion_t calculate_insert_type(
        nonstd::observer_ptr<tree_node_t> node, wf::point_t input)
    {
        return calculate_insert_type(node, input, SPLIT_PREVIEW_PERCENTAGE);
    }

    /**
     * Calculate the bounds of the split preview
     */
    wf::geometry_t calculate_split_preview(nonstd::observer_ptr<tree_node_t> over,
        split_insertion_t split_type)
    {
        auto preview = over->geometry;
        switch (split_type)
        {
          case INSERT_RIGHT:
            preview.x += preview.width * (1 - SPLIT_PREVIEW_PERCENTAGE);

          // fallthrough
          case INSERT_LEFT:
            preview.width = preview.width * SPLIT_PREVIEW_PERCENTAGE;
            break;

          case INSERT_BELOW:
            preview.y += preview.height * (1 - SPLIT_PREVIEW_PERCENTAGE);

          // fallthrough
          case INSERT_ABOVE:
            preview.height = preview.height * SPLIT_PREVIEW_PERCENTAGE;
            break;

          default:
            break; // nothing to do
        }

        return preview;
    }

    /**
     * Return the node under the input which is suitable for dropping on.
     */
    nonstd::observer_ptr<view_node_t> check_drop_destination(wf::output_t *output, wf::point_t global_coords,
        wayfire_toplevel_view dragged_view)
    {
        auto ws = output->wset()->get_current_workspace();
        auto dropped_at =
            find_view_at(tile_workspace_set_data_t::get(output).roots[ws.x][ws.y], global_coords);

        if (!dropped_at || (dropped_at->view == dragged_view))
        {
            return nullptr;
        }

        return dropped_at;
    }
};
}
}
