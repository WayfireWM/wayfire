#include <ctime>
#include <memory>
#include <wayfire/surface.hpp>

#include "surface-pointer-interaction.cpp"
#include "surface-touch-interaction.cpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/util.hpp"

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
        result.local_coords = local;
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
    wf::wl_listener_wrapper on_visibility_output_commit;
    wf::output_t *visible_on;
    damage_callback push_damage;

    wf::signal::connection_t<node_damage_signal> on_surface_damage =
        [=] (node_damage_signal *data)
    {
        if (auto wlr_surf = surface->get_wlr_surface())
        {
            // Make sure to expand damage, because stretching the surface may cause additional damage.
            const float scale = wlr_surf->current.scale;
            const float output_scale = visible_on ? visible_on->handle->scale : 1.0;
            if (scale != output_scale)
            {
                data->region.expand_edges(std::ceil(std::abs(scale - output_scale)));
            }
        }

        push_damage(data->region);
    };

  public:
    surface_render_instance_t(wf::surface_interface_t *si, damage_callback push_damage,
        wf::output_t *visible_on)
    {
        this->surface     = si;
        this->push_damage = push_damage;
        this->visible_on  = visible_on;
        auto node = si->priv->content_node;
        node->connect(&on_surface_damage);
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

    direct_scanout try_scanout(wf::output_t *output) override
    {
        const auto& desired_size = wf::dimensions(output->get_relative_geometry());
        if (surface->get_size() != desired_size)
        {
            return direct_scanout::OCCLUSION;
        }

        // Must have a wlr surface with the correct scale and transform
        auto wlr_surf = surface->get_wlr_surface();
        if (!wlr_surf ||
            (wlr_surf->current.scale != output->handle->scale) ||
            (wlr_surf->current.transform != output->handle->transform))
        {
            return direct_scanout::OCCLUSION;
        }

        // Finally, the opaque region must be the full surface.
        wf::region_t non_opaque = output->get_relative_geometry();
        non_opaque ^= surface->get_opaque_region(wf::point_t{0, 0});
        if (!non_opaque.empty())
        {
            return direct_scanout::OCCLUSION;
        }

        wlr_presentation_surface_sampled_on_output(
            wf::get_core().protocols.presentation, wlr_surf, output->handle);
        wlr_output_attach_buffer(output->handle, &wlr_surf->buffer->base);

        if (wlr_output_commit(output->handle))
        {
            return direct_scanout::SUCCESS;
        } else
        {
            return direct_scanout::OCCLUSION;
        }
    }

    void compute_visibility(wf::output_t *output, wf::region_t& visible) override
    {
        auto our_box = wf::construct_box({0, 0}, surface->get_size());
        on_visibility_output_commit.disconnect();

        if (!(visible & our_box).empty())
        {
            // We are visible on the given output => send wl_surface.frame on output frame, so that clients
            // can draw the next frame.
            on_visibility_output_commit.set_callback([=] (void *data)
            {
                if (surface->get_wlr_surface())
                {
                    timespec now;
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    wlr_surface_send_frame_done(surface->get_wlr_surface(), &now);
                }
            });
            on_visibility_output_commit.connect(&output->handle->events.frame);
            // TODO: compute actually visible region and disable damage reporting for that region.
        }
    }
};

void surface_node_t::gen_render_instances(
    std::vector<render_instance_uptr> & instances, damage_callback damage, wf::output_t *output)
{
    instances.push_back(std::make_unique<surface_render_instance_t>(this->si, damage, output));
}

wf::geometry_t wf::scene::surface_node_t::get_bounding_box()
{
    return wf::construct_box({0, 0}, si->get_size());
}
}
}
