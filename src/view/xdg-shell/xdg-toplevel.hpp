#pragma once

#include "wayfire/geometry.hpp"
#include "wayfire/util.hpp"
#include <wayfire/unstable/wlr-surface-node.hpp>
#include <memory>
#include <wayfire/toplevel.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

namespace wf
{
/**
 * A signal emitted on the xdg_toplevel after the committed state is applied.
 */
struct xdg_toplevel_applied_state_signal
{
    toplevel_state_t old_state;
};

class xdg_toplevel_t : public toplevel_t, public std::enable_shared_from_this<xdg_toplevel_t>
{
  public:
    xdg_toplevel_t(wlr_xdg_toplevel *toplevel,
        std::shared_ptr<wf::scene::wlr_surface_node_t> surface);
    void commit() override;
    void apply() override;
    wf::geometry_t calculate_base_geometry();
    void request_native_size();

    wf::dimensions_t get_min_size() override;
    wf::dimensions_t get_max_size() override;

  private:
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;
    scene::surface_state_t pending_state;

    void apply_pending_state();
    wf::dimensions_t get_current_wlr_toplevel_size();

    wf::wl_listener_wrapper on_surface_commit;
    wf::wl_listener_wrapper on_toplevel_destroy;
    wlr_xdg_toplevel *toplevel;
    wf::point_t wm_offset = {0, 0};

    void handle_surface_commit();
    uint32_t target_configure = 0;

    void emit_ready();
    bool pending_ready = false;
};
}
