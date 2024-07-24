#pragma once

#include <wayfire/toplevel-view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workarea.hpp>

namespace wf
{
namespace grid
{
/**
 * name: request
 * on: core
 * when: Emitted before move renders a grid indicator and sets the slot.
 * carried_out: true if a plugin can handle move request to grid.
 */
struct grid_request_signal
{
    /* True if a plugin handled this signal */
    bool carried_out = false;
};

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

/*
 * 7 8 9
 * 4 5 6
 * 1 2 3
 */
inline uint32_t get_tiled_edges_for_slot(uint32_t slot)
{
    if (slot == 0)
    {
        return 0;
    }

    uint32_t edges = wf::TILED_EDGES_ALL;
    if (slot % 3 == 0)
    {
        edges &= ~WLR_EDGE_LEFT;
    }

    if (slot % 3 == 1)
    {
        edges &= ~WLR_EDGE_RIGHT;
    }

    if (slot <= 3)
    {
        edges &= ~WLR_EDGE_TOP;
    }

    if (slot >= 7)
    {
        edges &= ~WLR_EDGE_BOTTOM;
    }

    return edges;
}

inline uint32_t get_slot_from_tiled_edges(uint32_t edges)
{
    for (int slot = 0; slot <= 9; slot++)
    {
        if (get_tiled_edges_for_slot(slot) == edges)
        {
            return slot;
        }
    }

    return 0;
}

/*
 * 7 8 9
 * 4 5 6
 * 1 2 3
 * */
inline wf::geometry_t get_slot_dimensions(wf::output_t *output, int n)
{
    auto area = output->workarea->get_workarea();
    int w2    = area.width / 2;
    int h2    = area.height / 2;

    if (n % 3 == 1)
    {
        area.width = w2;
    }

    if (n % 3 == 0)
    {
        area.width = w2, area.x += w2;
    }

    if (n >= 7)
    {
        area.height = h2;
    } else if (n <= 3)
    {
        area.height = h2, area.y += h2;
    }

    return area;
}
}
}
