#pragma once

#include <wayfire/unstable/wlr-surface-node.hpp>

namespace wf
{
namespace xw
{
/**
 * A custom surface node class for Xwayland surfaces.
 * It is almost identical to wlr_surface_node_t, but it applies scaling to the coordinates to accomodate
 * for the fake Xwayland scaling that workarounds/force_xwayland_scaling introduces.
 */
class xwayland_surface_node_t : public wf::scene::wlr_surface_node_t
{
  public:
    using wlr_surface_node_t::wlr_surface_node_t;

    std::string stringify() const override;
    wf::pointf_t to_local(const wf::pointf_t& point) override;
    wf::pointf_t to_global(const wf::pointf_t& point) override;
    void apply_state(scene::surface_state_t&& state) override;

    void set_scale(float scale);

  private:
    float current_scale = 1.0f;
};
}
}
