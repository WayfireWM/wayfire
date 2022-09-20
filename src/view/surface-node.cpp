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
    this->ptr_interaction = std::make_unique<surface_pointer_interaction_t>(si);
    this->tch_interaction = std::make_unique<surface_touch_interaction_t>(si);
}

wf::pointf_t wf::scene::surface_node_t::to_local(const wf::pointf_t& point)
{
    auto offset = this->si->get_offset();
    wf::pointf_t local = point;
    local.x -= offset.x;
    local.y -= offset.y;
    return local;
}

wf::pointf_t wf::scene::surface_node_t::to_global(const wf::pointf_t& point)
{
    auto offset = this->si->get_offset();
    wf::pointf_t local = point;
    local.x += offset.x;
    local.y += offset.y;
    return local;
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
        auto offset = surface->get_offset();

        // Make it all surface-local
        damage += -offset;
        wf::render_target_t our_target = target;
        our_target.geometry = our_target.geometry + -offset;
        auto our_box = wf::construct_box({0, 0}, surface->get_size());

        wf::region_t our_damage = damage & our_box;
        if (!our_damage.empty())
        {
            instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = our_target,
                        .damage   = std::move(our_damage),
                    });

            damage ^= surface->get_opaque_region({0, 0});
        }

        // Push damage back to parent coordinates
        damage += offset;
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region, wf::output_t *output) override
    {
        surface->simple_render(target, 0, 0, region);
    }
};

void surface_node_t::gen_render_instances(
    std::vector<render_instance_uptr> & instances, damage_callback damage)
{
    instances.push_back(std::make_unique<surface_render_instance_t>(this->si));
}

wf::geometry_t wf::scene::surface_node_t::get_bounding_box()
{
    // FIXME: currently, the view node 'stops' bounding box calls.
    // We can't implement them properly, because get_bounding_box() may be called
    // while the node itself is being constructed (because of damage to parent
    // when the content node is added). This should be fixed at some point,
    // preferably after view and surface are split.
    return {0, 0, 0, 0};
    return wf::construct_box({0, 0}, si->get_size());
}
}
}
