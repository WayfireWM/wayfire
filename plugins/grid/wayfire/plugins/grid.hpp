#pragma once

#include <wayfire/view.hpp>

namespace wf
{
namespace grid
{
/**
 * The slot where a view can be placed with grid.
 * BL = bottom-left, TR = top-right, etc.
 */
enum slot_t
{
    SLOT_NONE   = 0,
    SLOT_BL     = 1,
    SLOT_BOTTOM = 2,
    SLOT_BR     = 3,
    SLOT_LEFT   = 4,
    SLOT_CENTER = 5,
    SLOT_RIGHT  = 6,
    SLOT_TL     = 7,
    SLOT_TOP    = 8,
    SLOT_TR     = 9,
};

/**
 * name: grid-query-geometry
 * on: output
 * when: A plugin can emit this signal to ask the grid plugin to calculate
 *   the geometry of a given slot.
 */
struct grid_query_geometry_signal
{
    // The slot to calculate geometry for
    slot_t slot;

    // Will be filled in by grid
    wf::geometry_t out_geometry;
};

/**
 * name: grid-snap-view
 * on: output
 * when: A plugin can emit this signal to ask the grid plugin to snap the
 *   view to the given slot.
 */
struct grid_snap_view_signal
{
    wayfire_toplevel_view view;
    slot_t slot;
};
}
}
