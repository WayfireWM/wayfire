#ifndef SURFACE_IMPL_HPP
#define SURFACE_IMPL_HPP

#include "wayfire/scene.hpp"
#include <wayfire/opengl.hpp>
#include <wayfire/surface.hpp>
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

class surface_interface_t::impl
{
  public:
    surface_interface_t *parent_surface;
    std::vector<std::unique_ptr<surface_interface_t>> surface_children_above;
    std::vector<std::unique_ptr<surface_interface_t>> surface_children_below;
    size_t last_cnt_surfaces = 0;

    wf::scene::floating_inner_ptr root_node;
    wf::scene::node_ptr content_node;

    /**
     * Remove all subsurfaces and emit signals for them.
     */
    void clear_subsurfaces(surface_interface_t *self);

    /**
     * Most surfaces don't have a wlr_surface. However, internal surface
     * implementations can set the underlying surface so that functions like
     *
     * subtract_opaque(), send_frame_done(), etc. work for the surface
     */
    wlr_surface *wsurface = nullptr;
};
}

#endif /* end of include guard: SURFACE_IMPL_HPP */
