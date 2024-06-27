#ifndef VIEW_IMPL_HPP
#define VIEW_IMPL_HPP

#include <memory>
#include <wayfire/nonstd/safe-list.hpp>
#include <wayfire/view.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/toplevel.hpp>

#include "wayfire/core.hpp"
#include "wayfire/nonstd/tracking-allocator.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"
#include "wayfire/output.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/view-transform.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/toplevel-view.hpp>

struct wlr_seat;
namespace wf
{
/**
 * Private data used by the default view_interface_t implementation
 * TODO: split this into multiple classes, one for all views, one for wlr-backed ones.
 */
class view_interface_t::view_priv_impl
{
  public:
    bool is_mapped = false;
    wlr_surface *wsurface    = nullptr;
    size_t last_view_cnt     = 0;
    uint32_t allowed_actions = VIEW_ALLOW_ALL;

    uint32_t edges = 0;
    wlr_box minimize_hint = {0, 0, 0, 0};

    scene::floating_inner_ptr root_node;
    std::shared_ptr<scene::transform_manager_node_t> transformed_node;

    scene::node_ptr current_content;
    scene::node_ptr dummy_node;
    scene::floating_inner_ptr surface_root_node;
    wf::output_t *output;

    void set_mapped(wlr_surface *surface);
    void set_enabled(bool enabled);
    void set_mapped_surface_contents(std::shared_ptr<scene::wlr_surface_node_t> content);
    void unset_mapped_surface_contents();

    std::weak_ptr<wf::workspace_set_t> current_wset;
    std::shared_ptr<toplevel_t> toplevel;
    wf::signal::connection_t<destruct_signal<view_interface_t>> pre_free;
};

/**
 * Adjust the position of the view according to the new size of its buffer and the geometry.
 */
void adjust_geometry_for_gravity(wf::toplevel_state_t& desired_state, wf::dimensions_t actual_size);

void adjust_view_output_on_map(wf::toplevel_view_interface_t *self);
void adjust_view_pending_geometry_on_start_map(wf::toplevel_view_interface_t *self,
    wf::geometry_t map_geometry_client, bool map_fs, bool map_maximized);

/** Emit the map signal for the given view */
void init_xdg_shell();
void init_xwayland(bool lazy);
void init_layer_shell();
void fini_xdg_shell();
void fini_xwayland();
void fini_layer_shell();

std::string xwayland_get_display();
void xwayland_update_default_cursor();

/* Ensure that the given surface is on top of the Xwayland stack order. */
void xwayland_bring_to_front(wlr_surface *surface);
int xwayland_get_pid();

void init_desktop_apis();
void fini_desktop_apis();
void init_xdg_decoration_handlers();
void fini_xdg_decoration_handlers();

pointf_t place_popup_at(wlr_surface *parent, wlr_surface *popup, wf::pointf_t relative);
}

#endif /* end of include guard: VIEW_IMPL_HPP */
