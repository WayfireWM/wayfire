#pragma once

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
 * 1. A back-to-front iteration through the render tree to calculate the overall
 *   damaged region of the render tree.
 * 2. A front-to-back iteration through the render tree, so that every node
 *   calculates the parts of the destination buffer it should actually repaint.
 * 3. A final back-to-front iteration where the actual rendering happens.
 */
class render_instance_t
{
  public:
    virtual ~render_instance_t() = default;

    /**
     * Handle the first back-to-front iteration in a render pass.
     * Each render instance should add the region of damage for it and its
     * children to @accumulated_damage. It may also subtract from the damaged
     * region, if for example an opaque part of it covers already damaged areas.
     *
     * After collecting the damaged region, the render instance should 'reset'
     * the damage it has internally accumulated so far (but the damage should
     * remain in other render instances of the same node!).
     */
    virtual void collect_damage(wf::region_t& accumulated_damage) = 0;
};
}
}
