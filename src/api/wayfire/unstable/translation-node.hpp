#pragma once

#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>

namespace wf
{
namespace scene
{
/**
 * A node which simply applies an offset to its children.
 */
class translation_node_t : public wf::scene::floating_inner_node_t
{
  public:
    translation_node_t();

    /**
     * Set the offset the node applies to its children.
     * Note that damage is not automatically applied.
     */
    void set_offset(wf::point_t offset);

    /**
     * Get the current offset (set via @set_offset). Default offset is {0, 0}.
     */
    wf::point_t get_offset() const;

  public: // Implementation of node_t interface
    wf::pointf_t to_local(const wf::pointf_t& point) override;
    wf::pointf_t to_global(const wf::pointf_t& point) override;

    std::string stringify() const override;
    void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
        scene::damage_callback damage, wf::output_t *output) override;
    wf::geometry_t get_bounding_box() override;

  protected:
    wf::point_t offset = {0, 0};
};
}
}
