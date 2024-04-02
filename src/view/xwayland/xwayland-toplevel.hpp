#pragma once

#include "config.h"
#include "wayfire/geometry.hpp"
#include "wayfire/util.hpp"
#include <memory>
#include <wayfire/toplevel.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/unstable/wlr-surface-node.hpp>

#if WF_HAS_XWAYLAND

namespace wf
{
namespace xw
{
/**
 * A signal emitted on the xwayland_toplevel after the committed state is applied.
 */
struct xwayland_toplevel_applied_state_signal
{
    toplevel_state_t old_state;
};

class xwayland_toplevel_t : public wf::toplevel_t, public std::enable_shared_from_this<xwayland_toplevel_t>
{
  public:
    xwayland_toplevel_t(wlr_xwayland_surface *xw);
    void commit() override;
    void apply() override;

    wf::dimensions_t get_min_size() override;
    wf::dimensions_t get_max_size() override;

    void set_main_surface(std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface);
    void set_output_offset(wf::point_t output_offset);

    wf::geometry_t calculate_base_geometry();

    void request_native_size();

  private:
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;
    scene::surface_state_t pending_state;

    void apply_pending_state();
    wf::dimensions_t get_current_xw_size();

    wf::wl_listener_wrapper on_surface_commit;
    wf::wl_listener_wrapper on_xw_destroy;
    wf::wl_idle_call idle_ready;

    wlr_xwayland_surface *xw;
    wf::point_t output_offset = {0, 0};
    void handle_surface_commit();

    void reconfigure_xwayland_surface();
    void emit_ready();
    bool pending_ready = false;
};
}
}

#endif
