#pragma once

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
}
