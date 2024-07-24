#pragma once

#include "view/toplevel-node.hpp"
#include "wayfire/geometry.hpp"
#include <wayfire/view.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include "wayfire/unstable/wlr-surface-controller.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"
#include <wayfire/unstable/xdg-toplevel-base.hpp>

#include <wayfire/toplevel-view.hpp>
#include "wayfire/toplevel.hpp"
#include "wayfire/util.hpp"
#include "xdg-toplevel.hpp"

namespace wf
{
/**
 * An implementation of view_interface_t for xdg-shell toplevels.
 */
class xdg_toplevel_view_t : public xdg_toplevel_view_base_t, public wf::toplevel_view_interface_t
{
  public:
    static std::shared_ptr<xdg_toplevel_view_t> create(wlr_xdg_toplevel *toplevel);

    void request_native_size() override;
    void set_activated(bool active) override;
    bool should_be_decorated() override;
    void set_decoration_mode(bool use_csd);
    bool is_mapped() const override;

    // start the map transaction
    void start_map_tx();
    // start the unmap transaction
    void start_unmap_tx();

  private:
    friend class wf::tracking_allocator_t<view_interface_t>;
    xdg_toplevel_view_t(wlr_xdg_toplevel *tlvl);
    bool has_client_decoration = true;
    wf::wl_listener_wrapper on_request_move, on_request_resize, on_request_minimize, on_request_maximize,
        on_request_fullscreen, on_set_parent, on_show_window_menu;

    std::shared_ptr<wf::toplevel_view_node_t> surface_root_node;

    // A reference to 'this' used while unmapping, to ensure that the view lives until unmap happens.
    std::shared_ptr<wf::view_interface_t> _self_ref;

    std::shared_ptr<wf::xdg_toplevel_t> wtoplevel;
    wf::signal::connection_t<xdg_toplevel_applied_state_signal> on_toplevel_applied;

    void map() override;
    void destroy() override;
    void handle_toplevel_state_changed(toplevel_state_t old_state);
};

void default_handle_new_xdg_toplevel(wlr_xdg_toplevel *toplevel);
}
