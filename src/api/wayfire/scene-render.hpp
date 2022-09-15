#pragma once

#include "wayfire/nonstd/observer_ptr.h"
#include <memory>
#include <vector>
#include <wayfire/region.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/opengl.hpp>

namespace wf
{
namespace scene
{
/**
 * When (parts) of the scenegraph have to be rendered, they have to be
 * 'instantiated' first. The instantiation of a (sub)tree of the scenegraph
 * is a tree of render instances, called a render tree. The purpose of the
 * render trees is to enable damage tracking (each render instance has its own
 * damage), while allowing arbitrary transformations in the scenegraph (e.g. a
 * render instance does not need to export information about how it transforms
 * its children). Due to this design, render trees have to be regenerated every
 * time the relevant portion of the scenegraph changes.
 *
 * Actually painting a render tree (called render pass) is a process involving
 * three steps:
 *
 * 1. Calculate the damage accumulated from the render tree.
 * 2. A front-to-back iteration through the render tree, so that every node
 *   calculates the parts of the destination buffer it should actually repaint.
 * 3. A final back-to-front iteration where the actual rendering happens.
 */
class render_instance_t
{
  public:
    virtual ~render_instance_t() = default;
};

using render_instance_uptr = std::unique_ptr<render_instance_t>;

/**
 * A signal emitted when a part of the node is damaged.
 * on: the node itself.
 */
struct node_damage_signal
{
    wf::region_t region;
};

/**
 * Signal that a render pass starts.
 */
struct render_pass_begin_signal
{
    /**
     * The root instance of the render pass, can be used to identify the render pass.
     */
    nonstd::observer_ptr<render_instance_t> root_instance;
};
}
}
