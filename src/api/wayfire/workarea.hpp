#pragma once

#include <wayfire/geometry.hpp>
#include <functional>
#include <wayfire/output.hpp>

namespace wf
{
/**
 * Each output has a workarea manager which keeps track of the available workarea on that output. The
 * available area is typically the full output area minus any space reserved for panels, bars, etc.
 */
class output_workarea_manager_t
{
  public:
    /**
     * Special clients like panels can reserve place from an edge of the output.
     * It is used when calculating the dimensions of maximized/tiled windows and
     * others. The remaining space (which isn't reserved for panels) is called
     * the workarea.
     */
    enum anchored_edge
    {
        ANCHORED_EDGE_TOP    = 0,
        ANCHORED_EDGE_BOTTOM = 1,
        ANCHORED_EDGE_LEFT   = 2,
        ANCHORED_EDGE_RIGHT  = 3,
    };

    struct anchored_area
    {
        // The edge from which to reserve area.
        anchored_edge edge;

        // Amount of space to reserve.
        int reserved_size;

        // The reflowed callback is optional and when present, is called every time the anchored areas are
        // reflowed (e.g. anchored areas are recalculated). The passed geometry is the available workarea
        // before the view's own request was considered. That means, for the first anchored area in the
        // workarea manager, the geometry will be the full output's geometry. For each subsequent anchored
        // area, the size of the previous anchored areas is excluded from the passed available workarea.
        std::function<void(wf::geometry_t)> reflowed;
    };

    /**
     * Add a reserved area. The actual recalculation must be manually
     * triggered by calling reflow_reserved_areas()
     */
    void add_reserved_area(anchored_area *area);

    /**
     * Remove a reserved area. The actual recalculation must be manually
     * triggered by calling reflow_reserved_areas()
     */
    void remove_reserved_area(anchored_area *area);

    /**
     * Recalculate reserved area for each anchored area
     */
    void reflow_reserved_areas();

    /**
     * @return The free space of the output after reserving the space for panels
     */
    wf::geometry_t get_workarea();

    output_workarea_manager_t(wf::output_t *output);
    ~output_workarea_manager_t();

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}
