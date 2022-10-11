#include <memory>
#include <wayfire/surface.hpp>

#include "surface-pointer-interaction.cpp"
#include "surface-touch-interaction.cpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"

namespace wf
{
namespace scene
{
surface_node_t::surface_node_t(wf::surface_interface_t *si) : node_t(false)
{
    this->si = si;
    this->ptr_interaction =
        std::make_unique<surface_pointer_interaction_t>(si, this);
    this->tch_interaction = std::make_unique<surface_touch_interaction_t>(si);
}

std::optional<input_node_t> surface_node_t::find_node_at(
    const wf::pointf_t& at)
{
    auto local = to_local(at);
    if (si->accepts_input(std::round(local.x), std::round(local.y)))
    {
        wf::scene::input_node_t result;
        result.node    = this;
        result.surface = si;
        result.local_coords = at;
        return result;
    }

    return {};
}

std::string wf::scene::surface_node_t::stringify() const
{
    return "surface " + stringify_flags();
}

wf::pointer_interaction_t& wf::scene::surface_node_t::pointer_interaction()
{
    return *ptr_interaction;
}

wf::touch_interaction_t& wf::scene::surface_node_t::touch_interaction()
{
    return *tch_interaction;
}

class surface_render_instance_t : public render_instance_t
{
    wf::surface_interface_t *surface;

  public:
    surface_render_instance_t(wf::surface_interface_t *si)
    {
        this->surface = si;
    }

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        auto our_box = wf::construct_box({0, 0}, surface->get_size());

        wf::region_t our_damage = damage & our_box;
        if (!our_damage.empty())
        {
            instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = std::move(our_damage),
                    });

            damage ^= surface->get_opaque_region({0, 0});
        }
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region) override
    {
        surface->simple_render(target, 0, 0, region);
    }

    void presentation_feedback(wf::output_t *output) override
    {
        if (surface->get_wlr_surface() != nullptr)
        {
            wlr_presentation_surface_sampled_on_output(
                wf::get_core_impl().protocols.presentation,
                surface->get_wlr_surface(), output->handle);
        }
    }
};

void surface_node_t::gen_render_instances(
    std::vector<render_instance_uptr> & instances, damage_callback damage)
{
    instances.push_back(std::make_unique<surface_render_instance_t>(this->si));
}

wf::geometry_t wf::scene::surface_node_t::get_bounding_box()
{
    return wf::construct_box({0, 0}, si->get_size());
}
}
}
