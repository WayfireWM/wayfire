#pragma once

#include "wayfire/util.hpp"
#include <memory>
#include <wayfire/scene.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

namespace wf
{
/**
 * A subsurface root node. It applies a translation to its children equal to the offset of the subsurface.
 */
class wlr_subsurface_root_node_t : public wf::scene::floating_inner_node_t
{
  public:
    wlr_subsurface_root_node_t(wlr_subsurface *subsurface);

    wf::pointf_t to_local(const wf::pointf_t& point) override;
    wf::pointf_t to_global(const wf::pointf_t& point) override;

    std::string stringify() const override;
    void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
        scene::damage_callback damage, wf::output_t *output) override;
    wf::geometry_t get_bounding_box() override;
    wf::point_t get_offset();

  private:
    wlr_subsurface *subsurface;
    wf::wl_listener_wrapper on_subsurface_destroy;
};

/**
 * A class which manages a wlr_subsurface. Its lifetime is tied to the wlr_subsurface object.
 *
 * This class is responsible for managing the subsurface's state and enabling/disabling it when the subsurface
 * is mapped and unmapped. In addition, it should clean up the scenegraph when the subsurface is destroyed.
 */
class wlr_subsurface_controller_t
{
  public:
    wlr_subsurface_controller_t(wlr_subsurface *sub);
    std::shared_ptr<wlr_subsurface_root_node_t> get_subsurface_root();

  private:
    wlr_subsurface *sub;
    wl_listener_wrapper on_map, on_unmap, on_destroy;
    std::shared_ptr<wlr_subsurface_root_node_t> subsurface_root_node;
};
}
