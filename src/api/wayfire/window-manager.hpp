#pragma once

#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
namespace wf
{
/**
 * An interface which describes basic window management operations on toplevels.
 */
class window_manager_t
{
  public:
    virtual ~window_manager_t() = default;

    /**
     * Update the remembered last windowed geometry.
     *
     * When a view is being tiled or fullscreened, we usually want to remember its size and position so that
     * it can be restored to that geometry after unfullscreening/untiling. window-manager implementations keep
     * track of this when a plugin calls update_last_windowed_geometry().
     */
    virtual void update_last_windowed_geometry(wayfire_toplevel_view view);

    /**
     * Get the stored last_windowed_geometry, if it was stored at all.
     */
    virtual std::optional<wf::geometry_t> get_last_windowed_geometry(wayfire_toplevel_view view);
};
}
