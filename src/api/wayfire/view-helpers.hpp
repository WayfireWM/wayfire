#pragma once

#include "wayfire/scene.hpp"
#include <wayfire/view.hpp>

// This file contains helper functions which are helpful when working with views. Most of the operations are
// simply wrappers around more low-level functionality provided by views, scenegraph, etc.
namespace wf
{
/**
 * Find the scenegraph layer that the view is currently in.
 */
std::optional<wf::scene::layer> get_view_layer(wayfire_view view);
}
