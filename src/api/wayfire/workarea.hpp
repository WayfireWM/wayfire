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
        /* The edge from which to reserver area */
        anchored_edge edge;
        /* Amount of space to reserve */
        int reserved_size;

        /* Desired size, to be given later in the reflowed callback */
        int real_size;

        /* The reflowed callbacks allows the component registering the
         * anchored area to be notified whenever the dimensions or the position
         * of the anchored area changes.
         *
         * The first passed geometry is the geometry of the anchored area. The
         * second one is the available workarea at the moment that the current
         * workarea was considered. */
        std::function<void(wf::geometry_t, wf::geometry_t)> reflowed;
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
