#include "subsurface.hpp"
#include "view/view-impl.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"
#include <memory>
#include <wayfire/debug.hpp>
#include <cassert>

wf::wlr_subsurface_controller_t::wlr_subsurface_controller_t(wlr_subsurface *sub)
{
    if (sub->data)
    {
        delete (wlr_subsurface_controller_t*)sub->data;
    }

    this->sub = sub;
    sub->data = this;

    auto surface_node = std::make_shared<scene::wlr_surface_node_t>(sub->surface, true);
    if (!sub->mapped)
    {
        surface_node->set_enabled(false);
    }

    this->subsurface_root_node = std::make_shared<wlr_subsurface_root_node_t>(sub);
    subsurface_root_node->set_children_list({surface_node});

    on_map.set_callback([=] (void*)
    {
        wf::scene::set_node_enabled(surface_node, true);
    });
    on_unmap.set_callback([=] (void*)
    {
        wf::scene::set_node_enabled(surface_node, false);
    });

    on_destroy.set_callback([=] (void*)
    {
        wf::scene::remove_child(subsurface_root_node);
        delete this;
    });

    on_map.connect(&sub->events.map);
    on_unmap.connect(&sub->events.unmap);
    on_destroy.connect(&sub->events.destroy);
}

wf::wlr_subsurface_root_node_t::wlr_subsurface_root_node_t(wlr_subsurface *subsurface) :
    wf::scene::floating_inner_node_t(false)
{
    this->subsurface = subsurface;
    this->on_subsurface_destroy.set_callback([=] (void*)
    {
        this->subsurface = NULL;
        on_subsurface_destroy.disconnect();
    });

    on_subsurface_destroy.connect(&subsurface->events.destroy);
}

wf::pointf_t wf::wlr_subsurface_root_node_t::to_local(const wf::pointf_t& point)
{
    return point - wf::pointf_t{get_offset()};
}

wf::pointf_t wf::wlr_subsurface_root_node_t::to_global(const wf::pointf_t& point)
{
    return point + wf::pointf_t{get_offset()};
}

std::string wf::wlr_subsurface_root_node_t::stringify() const
{
    return "subsurface-root " + stringify_flags();
}

class wlr_subsurface_root_render_instance_t : public wf::scene::render_instance_t
{
    std::vector<wf::scene::render_instance_uptr> children;
    wf::scene::damage_callback push_damage;
    wf::wlr_subsurface_root_node_t *self;

  public:
    wlr_subsurface_root_render_instance_t(wf::wlr_subsurface_root_node_t *self,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on)
    {
        this->self = self;
        this->push_damage = push_damage;

        self->connect(&on_surface_damage);

        auto push_damage_child = [=] (wf::region_t child_damage)
        {
            child_damage += self->get_offset();
            push_damage(child_damage);
        };

        for (auto& ch : self->get_children())
        {
            if (ch->is_enabled())
            {
                ch->gen_render_instances(children, push_damage_child, shown_on);
            }
        }
    }

    wf::signal::connection_t<wf::scene::node_damage_signal> on_surface_damage =
        [=] (wf::scene::node_damage_signal *data)
    {
        push_damage(data->region);
    };

    void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        wf::point_t offset = self->get_offset();
        damage += -offset;

        auto our_target = target.translated(-offset);
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

    wf::scene::direct_scanout try_scanout(wf::output_t *output) override
    {
        if (self->get_offset() != wf::point_t{0, 0})
        {
            return wf::scene::direct_scanout::OCCLUSION;
        }

        return try_scanout_from_list(this->children, output);
    }

    void compute_visibility(wf::output_t *output, wf::region_t& visible) override
    {
        compute_visibility_from_list(children, output, visible, self->get_offset());
    }
};

void wf::wlr_subsurface_root_node_t::gen_render_instances(
    std::vector<scene::render_instance_uptr>& instances,
    scene::damage_callback damage, wf::output_t *output)
{
    instances.push_back(std::make_unique<wlr_subsurface_root_render_instance_t>(this, damage, output));
}

wf::geometry_t wf::wlr_subsurface_root_node_t::get_bounding_box()
{
    return get_children_bounding_box() + get_offset();
}

wf::point_t wf::wlr_subsurface_root_node_t::get_offset()
{
    if (subsurface)
    {
        return {
            subsurface->current.x,
            subsurface->current.y,
        };
    }

    return {0, 0};
}

std::shared_ptr<wf::wlr_subsurface_root_node_t> wf::wlr_subsurface_controller_t::get_subsurface_root()
{
    return this->subsurface_root_node;
}
