#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/object.hpp"
#include <wayfire/window-manager.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workarea.hpp>

namespace wf
{
class windowed_geometry_data_t : public wf::custom_data_t
{
  public:
    /** Last geometry the view has had in non-tiled and non-fullscreen state.
     * -1 as width/height means that no such geometry has been stored. */
    wf::geometry_t last_windowed_geometry = {0, 0, -1, -1};

    /**
     * The workarea when last_windowed_geometry was stored. This is used
     * for ex. when untiling a view to determine its geometry relative to the
     * (potentially changed) workarea of its output.
     */
    wf::geometry_t windowed_geometry_workarea = {0, 0, -1, -1};
};

void wf::window_manager_t::update_last_windowed_geometry(wayfire_toplevel_view view)
{
    if (!view->is_mapped() || view->tiled_edges || view->fullscreen)
    {
        return;
    }

    auto windowed = view->get_data_safe<windowed_geometry_data_t>();

    windowed->last_windowed_geometry = view->get_wm_geometry();
    if (view->get_output())
    {
        windowed->windowed_geometry_workarea = view->get_output()->workarea->get_workarea();
    } else
    {
        windowed->windowed_geometry_workarea = {0, 0, -1, -1};
    }
}

std::optional<wf::geometry_t> wf::window_manager_t::get_last_windowed_geometry(wayfire_toplevel_view view)
{
    auto windowed = view->get_data_safe<windowed_geometry_data_t>();

    if ((windowed->windowed_geometry_workarea.width <= 0) || (windowed->last_windowed_geometry.width <= 0))
    {
        return {};
    }

    if (!view->get_output())
    {
        return windowed->last_windowed_geometry;
    }

    const auto& geom     = windowed->last_windowed_geometry;
    const auto& old_area = windowed->windowed_geometry_workarea;
    const auto& new_area = view->get_output()->workarea->get_workarea();
    return wf::geometry_t{
        .x     = new_area.x + (geom.x - old_area.x) * new_area.width / old_area.width,
        .y     = new_area.y + (geom.y - old_area.y) * new_area.height / old_area.height,
        .width = geom.width * new_area.width / old_area.width,
        .height = geom.height * new_area.height / old_area.height
    };
}
}
