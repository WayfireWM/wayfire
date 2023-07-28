#pragma once

#include "wayfire/geometry.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include <wayfire/view.hpp>
#include <memory>
#include <wayfire/unstable/translation-node.hpp>
#include <wayfire/view-transform.hpp>

namespace wf
{
/**
 * A surface root node for toplevel views.
 */
class toplevel_view_node_t : public wf::scene::translation_node_t,
    public scene::zero_copy_texturable_node_t, public scene::opaque_region_node_t, public view_node_tag_t
{
  public:
    toplevel_view_node_t(wayfire_toplevel_view view);

    wf::keyboard_focus_node_t keyboard_refocus(wf::output_t *output) override;
    keyboard_interaction_t& keyboard_interaction() override;
    std::string stringify() const override;

    void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
        scene::damage_callback push_damage, wf::output_t *output) override;

    std::optional<wf::texture_t> to_texture() const override;
    wf::region_t get_opaque_region() const override;

  protected:
    std::weak_ptr<toplevel_view_interface_t> _view;
    std::unique_ptr<keyboard_interaction_t> kb_interaction;
};
}
