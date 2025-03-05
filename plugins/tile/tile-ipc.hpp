#pragma once

#include <set>
#include <wayfire/workspace-set.hpp>
#include <wayfire/toplevel-view.hpp>
#include "tree.hpp"
#include "plugins/ipc/ipc-helpers.hpp"
#include "plugins/ipc/ipc-method-repository.hpp"
#include "tile-wset.hpp"

namespace wf
{
namespace tile
{
struct json_builder_data_t
{
    std::set<workspace_set_t*> touched_wsets;
    std::set<wayfire_toplevel_view> touched_views;
    gap_size_t gaps;
};

/**
 * Get a json description of the given tiling tree.
 */
inline wf::json_t tree_to_json(const std::unique_ptr<tree_node_t>& root,
    const wf::point_t& offset,
    double rel_size = 1.0)
{
    wf::json_t js;
    js["percent"]  = rel_size;
    js["geometry"] = wf::ipc::geometry_to_json(root->geometry - offset);
    if (auto view = root->as_view_node())
    {
        js["view-id"] = view->view->get_id();
        return js;
    }

    auto split = root->as_split_node();
    wf::dassert(split != nullptr, "Expected to be split node");

    wf::json_t children = wf::json_t::array();
    if (split->get_split_direction() == SPLIT_HORIZONTAL)
    {
        for (auto& child : split->children)
        {
            children.append(
                tree_to_json(child, offset, 1.0 * child->geometry.height / split->geometry.height));
        }

        js["horizontal-split"] = std::move(children);
    } else
    {
        for (auto& child : split->children)
        {
            children.append(tree_to_json(
                child, offset, 1.0 * child->geometry.width / split->geometry.width));
        }

        js["vertical-split"] = std::move(children);
    }

    return js;
}

/**
 * Go over the json description and verify that it is a valid tiling tree.
 * @return An error message if the tree is invalid.
 */
inline std::optional<std::string> verify_json_tree(const json_reference_t& json,
    json_builder_data_t& data, const wf::dimensions_t& available_geometry)
{
    if (!json.is_object())
    {
        return "JSON Tree structure is wrong!";
    }

    if ((available_geometry.width <= data.gaps.left + data.gaps.right) ||
        (available_geometry.height <= data.gaps.top + data.gaps.bottom))
    {
        return "Geometry becomes too small for some nodes!";
    }

    json["width"]  = available_geometry.width;
    json["height"] = available_geometry.height;
    if (json.has_member("view-id"))
    {
        if (!json["view-id"].is_uint64())
        {
            return "view-id should be unsigned integer!";
        }

        auto view = toplevel_cast(wf::ipc::find_view_by_id(json["view-id"]));
        if (!view)
        {
            return "No view found with id " + std::to_string((uint32_t)json["view-id"]);
        }

        if (!view->toplevel()->pending().mapped)
        {
            return "Cannot tile pending-unmapped views!";
        }

        if (data.touched_views.count(view))
        {
            return "View tiled twice!";
        }

        data.touched_views.insert(view);
        data.touched_wsets.insert(view->get_wset().get());
        return {};
    }

    const bool is_horiz_split = json.has_member("horizontal-split") && json["horizontal-split"].is_array();
    const bool is_vert_split  = json.has_member("vertical-split") && json["vertical-split"].is_array();
    if (!is_horiz_split && !is_vert_split)
    {
        return "Node is neither a view, nor a split node!";
    }

    int32_t split_axis = is_horiz_split ? available_geometry.height : available_geometry.width;
    double weight_sum  = 0;

    const auto& children_list = is_horiz_split ? json["horizontal-split"] : json["vertical-split"];
    for (size_t i = 0; i < children_list.size(); i++)
    {
        if (!children_list[i].has_member("weight"))
        {
            return "Expected 'weight' field for each child node!";
        }

        if (!children_list[i]["weight"].is_double())
        {
            return "Expected 'weight' field to be a number!";
        }

        weight_sum += double(children_list[i]["weight"]);
    }

    int32_t size_sum = 0;
    for (size_t i = 0; i < children_list.size(); i++)
    {
        int32_t size = double(children_list[i]["weight"]) / weight_sum * split_axis;
        size_sum += size;
        if (i == children_list.size() - 1)
        {
            // This is needed because of rounding errors, we always round down, but in the end, we need to
            // make sure that the nodes cover the whole screen.
            size += split_axis - size_sum;
        }

        const wf::dimensions_t available_for_child = is_horiz_split ?
            wf::dimensions_t{available_geometry.width, size} :
        wf::dimensions_t{size, available_geometry.height};

        auto error = verify_json_tree(children_list[i], data, available_for_child);
        if (error.has_value())
        {
            return error;
        }
    }

    // All was OK
    return {};
}

inline std::unique_ptr<tile::tree_node_t> build_tree_from_json_rec(const json_reference_t& json,
    tile_workspace_set_data_t *wdata, wf::point_t vp)
{
    std::unique_ptr<tile::tree_node_t> root;

    if (json.has_member("view-id"))
    {
        auto view = toplevel_cast(wf::ipc::find_view_by_id(json["view-id"]));
        root = wdata->setup_view_tiling(view, vp);
    } else
    {
        const bool is_horiz_split = json.has_member("horizontal-split");
        const auto& children_list = is_horiz_split ? json["horizontal-split"] : json["vertical-split"];
        auto split_parent = std::make_unique<tile::split_node_t>(
            is_horiz_split ? tile::SPLIT_HORIZONTAL : tile::SPLIT_VERTICAL);

        for (size_t i = 0; i < children_list.size(); i++)
        {
            split_parent->children.push_back(build_tree_from_json_rec(children_list[i], wdata, vp));
            split_parent->children.back()->parent = {split_parent.get()};
        }

        root = std::move(split_parent);
    }

    root->geometry.x     = 0;
    root->geometry.y     = 0;
    root->geometry.width = json["width"];
    root->geometry.height = json["height"];
    return root;
}

/**
 * Build a tiling tree from a json description.
 *
 * Note that the tree description first has to be verified and pre-processed by verify_json_tree().
 */
inline std::unique_ptr<tile::tree_node_t> build_tree_from_json(const wf::json_t& json,
    tile_workspace_set_data_t *wdata, wf::point_t vp)
{
    auto root = build_tree_from_json_rec(json, wdata, vp);
    if (root->as_view_node())
    {
        // Handle cases with a single view.
        auto split_root = std::make_unique<tile::split_node_t>(tile_workspace_set_data_t::default_split);
        split_root->children.push_back(std::move(root));
        return split_root;
    }

    return root;
}

inline wf::json_t handle_ipc_get_layout(const json_t& params)
{
    auto wset_index = wf::ipc::json_get_uint64(params, "wset-index");

    if (!params.has_member("workspace") or !params["workspace"].is_object())
    {
        return wf::ipc::json_error("Missing 'workspace' field");
    }

    auto x  = wf::ipc::json_get_int64(params["workspace"], "x");
    auto y  = wf::ipc::json_get_int64(params["workspace"], "y");
    auto ws = ipc::find_workspace_set_by_index(wset_index);
    if (ws)
    {
        auto grid_size = ws->get_workspace_grid_size();
        if ((x >= grid_size.width) || (y >= grid_size.height))
        {
            return wf::ipc::json_error("invalid workspace coordinates");
        }

        auto response = wf::ipc::json_ok();

        auto cur_ws     = ws->get_current_workspace();
        auto resolution = ws->get_last_output_geometry().value_or(tile::default_output_resolution);
        wf::point_t offset = {cur_ws.x * resolution.width, cur_ws.y * resolution.height};

        response["layout"] =
            tree_to_json(tile_workspace_set_data_t::get(ws->shared_from_this()).roots[x][y], offset);
        return response;
    }

    return wf::ipc::json_error("wset-index not found");
}

inline wf::json_t handle_ipc_set_layout(json_t params)
{
    auto wset_index = wf::ipc::json_get_uint64(params, "wset-index");
    if (!params.has_member("workspace") or !params["workspace"].is_object())
    {
        return wf::ipc::json_error("Missing 'workspace' field");
    }

    auto x = wf::ipc::json_get_int64(params["workspace"], "x");
    auto y = wf::ipc::json_get_int64(params["workspace"], "y");

    if (!params.has_member("layout") || !params["layout"].is_object())
    {
        return wf::ipc::json_error("Missing 'layout' field");
    }

    auto ws = ipc::find_workspace_set_by_index(wset_index);
    if (!ws)
    {
        return wf::ipc::json_error("wset-index not found");
    }

    auto grid_size = ws->get_workspace_grid_size();
    if ((x >= grid_size.width) || (y >= grid_size.height))
    {
        return wf::ipc::json_error("invalid workspace coordinates");
    }

    auto& tile_ws = tile_workspace_set_data_t::get(ws->shared_from_this());

    tile::json_builder_data_t data;
    data.gaps = tile_ws.get_gaps();
    auto workarea = tile_ws.roots[x][y]->geometry;
    if (auto err = tile::verify_json_tree(params["layout"], data, wf::dimensions(workarea)))
    {
        return wf::ipc::json_error(*err);
    }

    // Step 1: detach any views which are currently present in the layout, but should no longer be
    // in the layout
    std::vector<nonstd::observer_ptr<tile::view_node_t>> views_to_remove;
    tile::for_each_view(tile_ws.roots[x][y], [&] (wayfire_toplevel_view view)
    {
        if (!data.touched_views.count(view))
        {
            views_to_remove.push_back(tile::view_node_t::get_node(view));
        }
    });

    tile_ws.detach_views(views_to_remove);

    {
        autocommit_transaction_t tx;
        data.touched_wsets.erase(nullptr);

        // Step 2: temporarily detach some of the nodes
        for (auto& touched_view : data.touched_views)
        {
            auto tile = wf::tile::view_node_t::get_node(touched_view);
            if (tile)
            {
                tile->parent->remove_child(tile, tx.tx);
            }

            if (touched_view->get_wset().get() != ws)
            {
                auto old_wset = touched_view->get_wset();
                wf::emit_view_pre_moved_to_wset_pre(touched_view,
                    touched_view->get_wset(), ws->shared_from_this());

                if (old_wset)
                {
                    old_wset->remove_view(touched_view);
                }

                ws->add_view(touched_view);
                wf::emit_view_moved_to_wset(touched_view, old_wset, ws->shared_from_this());
            }
        }

        // Step 3: set up the new layout
        tile_ws.roots[x][y] = build_tree_from_json(params["layout"], &tile_ws, {static_cast<int>(x),
            static_cast<int>(y)});
        tile::flatten_tree(tile_ws.roots[x][y]);
        tile_ws.roots[x][y]->set_gaps(tile_ws.get_gaps());
        tile_ws.roots[x][y]->set_geometry(workarea, tx.tx);
    }

    data.touched_wsets.insert(ws);

    // Step 4: flatten roots, set gaps, trigger resize everywhere
    for (auto& touched_ws : data.touched_wsets)
    {
        auto& tws = tile_workspace_set_data_t::get(touched_ws->shared_from_this());
        tws.flatten_roots();
        // will also trigger resize everywhere
        tws.update_gaps();
    }

    return wf::ipc::json_ok();
}
}
}
