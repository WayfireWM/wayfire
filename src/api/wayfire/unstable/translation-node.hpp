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
    translation_node_t(bool is_structure = false);

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
    uint32_t optimize_update(uint32_t flags) override;

  protected:
    wf::point_t offset = {0, 0};
};

class translation_node_instance_t : public render_instance_t
{
  protected:
    std::vector<render_instance_uptr> children;
    damage_callback push_damage;
    std::shared_ptr<translation_node_t> self;
    wf::signal::connection_t<wf::scene::node_damage_signal> on_node_damage;
    wf::signal::connection_t<wf::scene::node_regen_instances_signal> on_regen_instances;
    wf::output_t *shown_on;
    void regen_instances();

  public:
    translation_node_instance_t(translation_node_t *self,
        damage_callback push_damage, wf::output_t *shown_on);

    // Implementation of render_instance_t
    void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override;
    void render(const wf::render_target_t& target, const wf::region_t& region) override;
    void presentation_feedback(wf::output_t *output) override;
    wf::scene::direct_scanout try_scanout(wf::output_t *output) override;
    void compute_visibility(wf::output_t *output, wf::region_t& visible) override;
};
}
}
