#include "surface-impl.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/scene.hpp"
#include <memory>
#include <wayfire/surface.hpp>

namespace wf
{
namespace scene
{
surface_root_node_t::surface_root_node_t(surface_interface_t *si) :
    floating_inner_node_t(false)
{
    // FIXME: this is a hack to avoid computing damage while creating the nodes
    // in the constructor of surface_interface_t. We should add proper
    // initialization mechanisms, though that will happen later.
    this->children = {si->priv->content_node};
    si->priv->content_node->_parent = this;
    this->si = si;
}

wf::pointf_t surface_root_node_t::to_local(const wf::pointf_t& point)
{
    auto offset = this->si->get_offset();
    wf::pointf_t local = point;
    local.x -= offset.x;
    local.y -= offset.y;
    return local;
}

wf::pointf_t surface_root_node_t::to_global(const wf::pointf_t& point)
{
    auto offset = this->si->get_offset();
    wf::pointf_t local = point;
    local.x += offset.x;
    local.y += offset.y;
    return local;
}

std::string surface_root_node_t::stringify() const
{
    return "surface-root " + this->stringify_flags();
}

class surface_root_render_instance_t : public render_instance_t
{
    std::vector<render_instance_uptr> children;
    damage_callback push_damage;
    surface_interface_t *si;

  public:
    surface_root_render_instance_t(wf::surface_interface_t *si,
        damage_callback push_damage)
    {
        this->si = si;
        this->push_damage = push_damage;

        auto root_node = si->priv->root_node;
        root_node->connect(&on_surface_damage);

        auto push_damage_child = [=] (wf::region_t child_damage)
        {
            child_damage += si->get_offset();
            push_damage(child_damage);
        };

        for (auto& ch : root_node->get_children())
        {
            if (ch->is_enabled())
            {
                ch->gen_render_instances(children, push_damage_child);
            }
        }
    }

    wf::signal::connection_t<node_damage_signal> on_surface_damage =
        [=] (node_damage_signal *data)
    {
        push_damage(data->region);
    };

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        wf::render_target_t our_target = target;
        wf::point_t offset = si->get_offset();

        damage += -offset;
        our_target.geometry = our_target.geometry + -offset;
        for (auto& ch : this->children)
        {
            ch->schedule_instructions(instructions, our_target, damage);
        }

        damage += offset;
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region) override
    {
        wf::dassert(false, "Rendering a surface root node?");
    }

    void presentation_feedback(wf::output_t *output) override
    {
        for (auto& ch : this->children)
        {
            ch->presentation_feedback(output);
        }
    }
};

void surface_root_node_t::gen_render_instances(
    std::vector<render_instance_uptr>& instances, damage_callback damage,
    wf::output_t *output)
{
    instances.push_back(
        std::make_unique<surface_root_render_instance_t>(si, damage));
}

wf::geometry_t surface_root_node_t::get_bounding_box()
{
    return get_children_bounding_box() + si->get_offset();
}
}
}
