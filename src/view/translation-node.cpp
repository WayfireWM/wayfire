#include <string>
#include <wayfire/scene.hpp>
#include <wayfire/unstable/translation-node.hpp>
#include <wayfire/debug.hpp>

wf::scene::translation_node_t::translation_node_t() : wf::scene::floating_inner_node_t(false)
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

namespace wf
{
namespace scene
{
class translation_node_instance_t : public wf::scene::render_instance_t
{
    std::vector<wf::scene::render_instance_uptr> children;
    wf::scene::damage_callback push_damage;
    wf::scene::translation_node_t *self;

  public:
    translation_node_instance_t(wf::scene::translation_node_t *self,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on)
    {
        this->self = self;
        this->push_damage = push_damage;

        self->connect(&on_node_damage);

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

    wf::signal::connection_t<wf::scene::node_damage_signal> on_node_damage =
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
        wf::dassert(false, "Rendering a translation node?");
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
}
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
