#pragma once

#include "wayfire/util.hpp"
#include <wayfire/scene.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

namespace wf
{
namespace scene
{
/**
 * An implementation of node_t for wlr_surfaces.
 *
 * The node typically does not have children and displays a single surface. It is assumed that the surface is
 * positioned at (0, 0), which means this node usually should be put with a parent node which manages the
 * position in the scenegraph.
 */
class wlr_surface_node_t : public node_t
{
  public:
    wlr_surface_node_t(wlr_surface *surface);

    std::optional<input_node_t> find_node_at(const wf::pointf_t& at) override;

    std::string stringify() const override;
    pointer_interaction_t& pointer_interaction() override;
    touch_interaction_t& touch_interaction() override;

    void gen_render_instances(std::vector<render_instance_uptr>& instances, damage_callback damage,
        wf::output_t *output) override;
    wf::geometry_t get_bounding_box() override;

    wlr_surface *get_surface() const;

  private:
    std::unique_ptr<pointer_interaction_t> ptr_interaction;
    std::unique_ptr<touch_interaction_t> tch_interaction;
    wlr_surface *surface;
    std::map<wf::output_t*, int> visibility;
    class wlr_surface_render_instance_t;
    wf::wl_listener_wrapper on_surface_destroyed;
    wf::wl_listener_wrapper on_surface_commit;

    void send_frame_done();
};
}
}
