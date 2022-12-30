#pragma once

#include "wayfire/scene.hpp"
#include <wayfire/util.hpp>

namespace wf
{
struct node_recheck_constraints_signal
{};

/**
 * A class for managing a wlr_surface.
 * It is responsible for adding subsurfaces to it.
 */
class wlr_surface_controller_t
{
  public:
    wlr_surface_controller_t(wlr_surface *surface, scene::floating_inner_ptr root_node);
    ~wlr_surface_controller_t();

  private:
    scene::floating_inner_ptr root;
    wlr_surface *surface;

    wf::wl_listener_wrapper on_destroy;
    wf::wl_listener_wrapper on_new_subsurface;
};
}
