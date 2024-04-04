#include <string>
#include <wayfire/scene.hpp>
#include <wayfire/unstable/translation-node.hpp>
#include <wayfire/debug.hpp>

wf::scene::translation_node_t::translation_node_t(bool is_structure) :
    wf::scene::floating_inner_node_t(is_structure)
{}

wf::pointf_t wf::scene::translation_node_t::to_local(const wf::pointf_t& point)
{
    return point - wf::pointf_t{get_offset()};
}

wf::pointf_t wf::scene::translation_node_t::to_global(const wf::pointf_t& point)
{
    return point + wf::pointf_t{get_offset()};
}

std::string wf::scene::translation_node_t::stringify() const
{
    auto x = std::to_string(get_offset().x);
    auto y = std::to_string(get_offset().y);
    return "translation by " + x + "," + y + " " + stringify_flags();
}

void wf::scene::translation_node_t::gen_render_instances(
    std::vector<scene::render_instance_uptr>& instances,
    scene::damage_callback damage, wf::output_t *output)
{
    instances.push_back(std::make_unique<translation_node_instance_t>(this, damage, output));
}

wf::geometry_t wf::scene::translation_node_t::get_bounding_box()
{
    return get_children_bounding_box() + get_offset();
}

wf::point_t wf::scene::translation_node_t::get_offset() const
{
    return offset;
}

void wf::scene::translation_node_t::set_offset(wf::point_t offset)
{
    this->offset = offset;
}

uint32_t wf::scene::translation_node_t::optimize_update(uint32_t flags)
{
    return optimize_nested_render_instances(shared_from_this(), flags);
}

// ----------------------------------------- Render instance -------------------------------------------------
wf::scene::translation_node_instance_t::translation_node_instance_t(
    translation_node_t *self, damage_callback push_damage, wf::output_t *shown_on)
{
    this->self = std::dynamic_pointer_cast<translation_node_t>(self->shared_from_this());
    this->push_damage = push_damage;
    this->shown_on    = shown_on;

    on_node_damage = [=] (wf::scene::node_damage_signal *data)
    {
        push_damage(data->region);
    };
    self->connect(&on_node_damage);

    on_regen_instances = [=] (auto)
    {
        regen_instances();
    };
    self->connect(&on_regen_instances);
    regen_instances();
}

void wf::scene::translation_node_instance_t::regen_instances()
{
    children.clear();
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

void wf::scene::translation_node_instance_t::schedule_instructions(
    std::vector<wf::scene::render_instruction_t>& instructions,
    const wf::render_target_t& target, wf::region_t& damage)
{
    wf::region_t our_damage = damage & self->get_bounding_box();
    if (!our_damage.empty())
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
}

void wf::scene::translation_node_instance_t::render(
    const wf::render_target_t& target, const wf::region_t& region)
{
    wf::dassert(false, "Rendering a translation node?");
}

void wf::scene::translation_node_instance_t::presentation_feedback(wf::output_t *output)
{
    for (auto& ch : this->children)
    {
        ch->presentation_feedback(output);
    }
}

wf::scene::direct_scanout wf::scene::translation_node_instance_t::try_scanout(wf::output_t *output)
{
    if (self->get_offset() != wf::point_t{0, 0})
    {
        return wf::scene::direct_scanout::OCCLUSION;
    }

    return try_scanout_from_list(this->children, output);
}

void wf::scene::translation_node_instance_t::compute_visibility(wf::output_t *output, wf::region_t& visible)
{
    compute_visibility_from_list(children, output, visible, self->get_offset());
}
