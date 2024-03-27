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
    static void create_controller(wlr_surface *surface, scene::floating_inner_ptr root_node);
    static void try_free_controller(wlr_surface *surface);

  private:
    wlr_surface_controller_t(wlr_surface *surface, scene::floating_inner_ptr root_node);
    ~wlr_surface_controller_t();

    void update_subsurface_order_and_position();

    scene::floating_inner_ptr root;
    wlr_surface *surface;

    wf::wl_listener_wrapper on_destroy;
    wf::wl_listener_wrapper on_new_subsurface;
    wf::wl_listener_wrapper on_commit;
};
}
