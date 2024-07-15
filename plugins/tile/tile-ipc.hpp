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
inline nlohmann::json tree_to_json(const std::unique_ptr<tree_node_t>& root, const wf::point_t& offset,
    double rel_size = 1.0)
{
    nlohmann::json js;
    js["percent"]  = rel_size;
    js["geometry"] = wf::ipc::geometry_to_json(root->geometry - offset);
    if (auto view = root->as_view_node())
    {
        js["view-id"] = view->view->get_id();
        return js;
    }

    auto split = root->as_split_node();
    wf::dassert(split != nullptr, "Expected to be split node");

    nlohmann::json children = nlohmann::json::array();
    if (split->get_split_direction() == SPLIT_HORIZONTAL)
    {
        for (auto& child : split->children)
        {
            children.push_back(
                tree_to_json(child, offset, 1.0 * child->geometry.height / split->geometry.height));
        }

        js["horizontal-split"] = std::move(children);
    } else
    {
        for (auto& child : split->children)
        {
            children.push_back(tree_to_json(
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
inline std::optional<std::string> verify_json_tree(nlohmann::json& json, json_builder_data_t& data,
    const wf::dimensions_t& available_geometry)
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
    if (json.count("view-id"))
    {
        if (!json["view-id"].is_number_unsigned())
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

    const bool is_horiz_split = json.count("horizontal-split") && json["horizontal-split"].is_array();
    const bool is_vert_split  = json.count("vertical-split") && json["vertical-split"].is_array();
    if (!is_horiz_split && !is_vert_split)
    {
        return "Node is neither a view, nor a split node!";
    }

    int32_t split_axis = is_horiz_split ? available_geometry.height : available_geometry.width;
    double weight_sum  = 0;

    auto& children_list = is_horiz_split ? json["horizontal-split"] : json["vertical-split"];

    for (auto& child : children_list)
    {
        if (!child.count("weight"))
        {
            return "Expected 'weight' field for each child node!";
        }

        if (!child["weight"].is_number())
        {
            return "Expected 'weight' field to be a number!";
        }

        weight_sum += float(child["weight"]);
    }

    int32_t size_sum = 0;
    for (auto& child : children_list)
    {
        int32_t size = float(child["weight"]) / weight_sum * split_axis;
        size_sum += size;
        if (&child == &children_list.back())
        {
            // This is needed because of rounding errors, we always round down, but in the end, we need to
            // make sure that the nodes cover the whole screen.
            size += split_axis - size_sum;
        }

        const wf::dimensions_t available_for_child = is_horiz_split ?
            wf::dimensions_t{available_geometry.width, size} :
        wf::dimensions_t{size, available_geometry.height};

        auto error = verify_json_tree(child, data, available_for_child);
        if (error.has_value())
        {
            return error;
        }
    }

    // All was OK
    return {};
}

inline std::unique_ptr<tile::tree_node_t> build_tree_from_json_rec(const nlohmann::json& json,
    tile_workspace_set_data_t *wdata, wf::point_t vp)
{
    std::unique_ptr<tile::tree_node_t> root;

    if (json.count("view-id"))
    {
        auto view = toplevel_cast(wf::ipc::find_view_by_id(json["view-id"]));
        root = wdata->setup_view_tiling(view, vp);
    } else
    {
        const bool is_horiz_split = json.count("horizontal-split");
        auto& children_list = is_horiz_split ? json["horizontal-split"] : json["vertical-split"];
        auto split_parent   = std::make_unique<tile::split_node_t>(
            is_horiz_split ? tile::SPLIT_HORIZONTAL : tile::SPLIT_VERTICAL);

        for (auto& child : children_list)
        {
            split_parent->children.push_back(build_tree_from_json_rec(child, wdata, vp));
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
inline std::unique_ptr<tile::tree_node_t> build_tree_from_json(const nlohmann::json& json,
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

inline nlohmann::json handle_ipc_get_layout(const nlohmann::json& params)
{
    WFJSON_EXPECT_FIELD(params, "wset-index", number_unsigned);
    WFJSON_EXPECT_FIELD(params, "workspace", object);
    WFJSON_EXPECT_FIELD(params["workspace"], "x", number_unsigned);
    WFJSON_EXPECT_FIELD(params["workspace"], "y", number_unsigned);

    int x   = params["workspace"]["x"].get<int>();
    int y   = params["workspace"]["y"].get<int>();
    auto ws = ipc::find_workspace_set_by_index(params["wset-index"].get<int>());
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

inline nlohmann::json handle_ipc_set_layout(nlohmann::json params)
{
    WFJSON_EXPECT_FIELD(params, "wset-index", number_unsigned);
    WFJSON_EXPECT_FIELD(params, "workspace", object);
    WFJSON_EXPECT_FIELD(params["workspace"], "x", number_unsigned);
    WFJSON_EXPECT_FIELD(params["workspace"], "y", number_unsigned);
    WFJSON_EXPECT_FIELD(params, "layout", object);
    int x = params["workspace"]["x"].get<int>();
    int y = params["workspace"]["y"].get<int>();

    auto ws = ipc::find_workspace_set_by_index(params["wset-index"].get<int>());
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
        tile_ws.roots[x][y] = build_tree_from_json(params["layout"], &tile_ws, {x, y});
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
