#pragma once

#include "toplevel.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene.hpp"
#include <initializer_list>
#include <wayfire/view.hpp>

// This file contains helper functions which are helpful when working with views. Most of the operations are
// simply wrappers around more low-level functionality provided by views, scenegraph, etc.
namespace wf
{
/**
 * Find the scenegraph layer that the view is currently in.
 */
std::optional<wf::scene::layer> get_view_layer(wayfire_view view);

/**
 * Reorder the nodes on the path from the view to the scenegraph root so that the view is as high in the
 * stacking order as possible.
 *
 * Also damages the affected nodes.
 */
void view_bring_to_front(wayfire_view view);

/**
 * Iterate over all scenegraph nodes in the given scenegraph subtree and collect all enabled view nodes.
 * The nodes returned are in front-to-back order.
 */
std::vector<wayfire_view> collect_views_from_scenegraph(wf::scene::node_ptr root);

/**
 * Collect all views from the scenegraph nodes of the output for the given layers.
 */
std::vector<wayfire_view> collect_views_from_output(
    wf::output_t *output, std::initializer_list<wf::scene::layer> layers);

/**
 * Find the topmost parent in the chain of views.
 */
wayfire_view find_topmost_parent(wayfire_view v);
wayfire_toplevel_view find_topmost_parent(wayfire_toplevel_view v);

/**
 * A few simple functions which help in view implementations.
 */
namespace view_implementation
{
void emit_toplevel_state_change_signals(wayfire_toplevel_view view, const wf::toplevel_state_t& old_state);
void emit_view_map_signal(wayfire_view view, bool has_position);
void emit_ping_timeout_signal(wayfire_view view);
void emit_geometry_changed_signal(wayfire_toplevel_view view, wf::geometry_t old_geometry);
void emit_title_changed_signal(wayfire_view view);
void emit_app_id_changed_signal(wayfire_view view);
}
}
