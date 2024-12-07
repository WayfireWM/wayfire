#include "wayfire/unstable/wlr-surface-node.hpp"
#include "pixman.h"
#include "wayfire/geometry.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wlr-surface-pointer-interaction.hpp"
#include "wlr-surface-touch-interaction.cpp"
#include "wayfire/output-layout.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <sstream>
#include <string>
#include <wayfire/signal-provider.hpp>
#include <wlr/util/box.h>

wf::scene::surface_state_t::surface_state_t(surface_state_t&& other)
{
    if (&other != this)
    {
        *this = std::move(other);
    }
}

wf::scene::surface_state_t& wf::scene::surface_state_t::operator =(surface_state_t&& other)
{
    if (current_buffer)
    {
        wlr_buffer_unlock(current_buffer);
    }

    current_buffer = other.current_buffer;
    texture = other.texture;
    accumulated_damage = other.accumulated_damage;
    size = other.size;
    src_viewport = other.src_viewport;
    transform    = other.transform;

    other.current_buffer = NULL;
    other.texture = NULL;
    other.accumulated_damage.clear();
    other.src_viewport.reset();
    return *this;
}

void wf::scene::surface_state_t::merge_state(wlr_surface *surface)
{
    // NB: lock the new buffer first, in case it is the same as the old one
    if (surface->buffer)
    {
        wlr_buffer_lock(&surface->buffer->base);
    }

    if (current_buffer)
    {
        wlr_buffer_unlock(current_buffer);
    }

    if (surface->buffer)
    {
        this->current_buffer = &surface->buffer->base;
        this->texture = surface->buffer->texture;
        this->size    = {surface->current.width, surface->current.height};
        this->transform = {surface->current.transform};
    } else
    {
        this->current_buffer = NULL;
        this->texture = NULL;
        this->size    = {0, 0};
    }

    if (surface->current.viewport.has_src)
    {
        wlr_fbox fbox;
        wlr_surface_get_buffer_source_box(surface, &fbox);
        this->src_viewport = fbox;
    } else
    {
        this->src_viewport.reset();
    }

    wf::region_t current_damage;
    wlr_surface_get_effective_damage(surface, current_damage.to_pixman());
    this->accumulated_damage |= current_damage;
}

wf::scene::surface_state_t::~surface_state_t()
{
    if (current_buffer)
    {
        wlr_buffer_unlock(current_buffer);
    }
}

wf::scene::wlr_surface_node_t::wlr_surface_node_t(wlr_surface *surface, bool autocommit) :
    node_t(false), autocommit(autocommit)
{
    this->surface = surface;
    this->ptr_interaction = std::make_unique<wlr_surface_pointer_interaction_t>(surface, this);
    this->tch_interaction = std::make_unique<wlr_surface_touch_interaction_t>(surface);

    this->on_surface_destroyed.set_callback([=] (void*)
    {
        this->surface = NULL;
        this->ptr_interaction = std::make_unique<pointer_interaction_t>();
        this->tch_interaction = std::make_unique<touch_interaction_t>();

        on_surface_commit.disconnect();
        on_surface_destroyed.disconnect();
    });

    this->on_surface_commit.set_callback([=] (void*)
    {
        if (!wlr_surface_has_buffer(this->surface) && this->visibility.empty())
        {
            send_frame_done(false);
        }

        if (this->autocommit)
        {
            apply_current_surface_state();
        }

        for (auto& [wo, _] : visibility)
        {
            wo->render->schedule_redraw();
        }
    });

    on_surface_destroyed.connect(&surface->events.destroy);
    on_surface_commit.connect(&surface->events.commit);
    send_frame_done(false);

    current_state.merge_state(surface);

    on_output_remove.set_callback([&] (wf::output_removed_signal *ev)
    {
        visibility.erase(ev->output);
        pending_visibility_delta.erase(ev->output);
    });
    wf::get_core().output_layout->connect(&on_output_remove);
}

void wf::scene::wlr_surface_node_t::apply_state(surface_state_t&& state)
{
    const bool size_changed = current_state.size != state.size;
    this->current_state = std::move(state);
    wf::scene::damage_node(this, current_state.accumulated_damage);
    if (size_changed)
    {
        scene::update(this->shared_from_this(), scene::update_flag::GEOMETRY);
    }
}

void wf::scene::wlr_surface_node_t::apply_current_surface_state()
{
    surface_state_t state;
    state.merge_state(surface);
    this->apply_state(std::move(state));
}

std::optional<wf::scene::input_node_t> wf::scene::wlr_surface_node_t::find_node_at(const wf::pointf_t& at)
{
    if (!surface)
    {
        return {};
    }

    if (wlr_surface_point_accepts_input(surface, at.x, at.y))
    {
        wf::scene::input_node_t result;
        result.node = this;
        result.local_coords = at;
        return result;
    }

    return {};
}

std::string wf::scene::wlr_surface_node_t::stringify() const
{
    std::ostringstream name;
    name << "wlr-surface-node ";
    if (surface)
    {
        name << "surface";
    } else
    {
        name << "inert";
    }

    name << " " << stringify_flags();
    return name.str();
}

wf::pointer_interaction_t& wf::scene::wlr_surface_node_t::pointer_interaction()
{
    return *this->ptr_interaction;
}

wf::touch_interaction_t& wf::scene::wlr_surface_node_t::touch_interaction()
{
    return *this->tch_interaction;
}

void wf::scene::wlr_surface_node_t::send_frame_done(bool delay_until_vblank)
{
    if (!surface)
    {
        return;
    }

    if (!delay_until_vblank || visibility.empty())
    {
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_surface_send_frame_done(surface, &now);
    } else
    {
        for (auto& [wo, _] : visibility)
        {
            wlr_output_schedule_frame(wo->handle);
        }
    }
}

class wf::scene::wlr_surface_node_t::wlr_surface_render_instance_t : public render_instance_t
{
    std::shared_ptr<wlr_surface_node_t> self;
    wf::signal::connection_t<wf::frame_done_signal> on_frame_done = [=] (wf::frame_done_signal *ev)
    {
        self->send_frame_done(false);
    };

    wf::output_t *visible_on;
    damage_callback push_damage;
    wf::region_t last_visibility;

    wf::signal::connection_t<node_damage_signal> on_surface_damage =
        [=] (node_damage_signal *data)
    {
        if (self->surface)
        {
            // Make sure to expand damage, because stretching the surface may cause additional damage.
            const float scale = self->surface->current.scale;
            const float output_scale = visible_on ? visible_on->handle->scale : 1.0;
            if (scale != output_scale)
            {
                data->region.expand_edges(std::ceil(std::abs(scale - output_scale)));
            }
        }

        static wf::option_wrapper_t<bool> use_opaque_optimizations{
            "workarounds/enable_opaque_region_damage_optimizations"
        };

        if (use_opaque_optimizations)
        {
            push_damage(data->region & last_visibility);
        } else
        {
            push_damage(data->region);
        }
    };

  public:
    wlr_surface_render_instance_t(std::shared_ptr<wlr_surface_node_t> self,
        damage_callback push_damage, wf::output_t *visible_on)
    {
        if (visible_on)
        {
            self->handle_enter(visible_on);
        }

        this->self = self;
        this->push_damage = push_damage;
        this->visible_on  = visible_on;
        self->connect(&on_surface_damage);
    }

    ~wlr_surface_render_instance_t()
    {
        if (visible_on)
        {
            self->handle_leave(visible_on);
        }
    }

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        wf::region_t our_damage = damage & self->get_bounding_box();
        if (!our_damage.empty())
        {
            instructions.push_back(render_instruction_t{
                .instance = this,
                .target   = target,
                .damage   = std::move(our_damage),
            });

            if (self->surface)
            {
                pixman_region32_subtract(damage.to_pixman(), damage.to_pixman(),
                    &self->surface->opaque_region);
            }
        }
    }

    void render(const wf::render_target_t& target, const wf::region_t& region) override
    {
        if (!self->current_state.current_buffer)
        {
            return;
        }

        wf::geometry_t geometry = self->get_bounding_box();
        wf::texture_t texture{self->current_state.texture, self->current_state.src_viewport};

        glm::mat4 transform = target.get_orthographic_projection();

        if (self->current_state.transform)
        {
            const double cx     = geometry.x + geometry.width / 2.0;
            const double cy     = geometry.y + geometry.height / 2.0;
            const double aspect = geometry.width * 1.0 / geometry.height;

            // Center the surface in the coordinate system, rotate (preserving aspect ration)
            // according to transform, go back
            glm::mat4 surface_transform =
                glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, 0.0f)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(aspect, 1.0f, 1.0f)) *
                get_output_matrix_from_transform(self->current_state.transform) *
                glm::scale(glm::mat4(1.0f), glm::vec3(1.0 / aspect, 1.0f, 1.0f)) *
                glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, 0.0f));
            transform = transform * surface_transform;
        }

        OpenGL::render_begin(target);
        OpenGL::render_transformed_texture(texture, geometry, transform,
            glm::vec4(1.f), OpenGL::RENDER_FLAG_CACHED);

        // use GL_NEAREST for integer scale.
        // GL_NEAREST makes scaled text blocky instead of blurry, which looks better
        // but only for integer scale.
        if (target.scale - floor(target.scale) < 0.001)
        {
            GL_CALL(glTexParameteri(texture.target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
        }

        for (const auto& rect : region)
        {
            target.logic_scissor(wlr_box_from_pixman_box(rect));
            OpenGL::draw_cached();
        }

        OpenGL::clear_cached();
        OpenGL::render_end();
    }

    void presentation_feedback(wf::output_t *output) override
    {
        if (self->surface)
        {
            wlr_presentation_surface_scanned_out_on_output(self->surface, output->handle);
        }
    }

    direct_scanout try_scanout(wf::output_t *output) override
    {
        if (!self->surface)
        {
            return direct_scanout::SKIP;
        }

        if (self->get_bounding_box() != output->get_relative_geometry())
        {
            return direct_scanout::OCCLUSION;
        }

        // Must have a wlr surface with the correct scale and transform
        auto wlr_surf = self->surface;
        if ((wlr_surf->current.scale != output->handle->scale) ||
            (wlr_surf->current.transform != output->handle->transform))
        {
            return direct_scanout::OCCLUSION;
        }

        // Finally, the opaque region must be the full surface.
        wf::region_t non_opaque = output->get_relative_geometry();
        non_opaque ^= wf::region_t{&wlr_surf->opaque_region};
        if (!non_opaque.empty())
        {
            return direct_scanout::OCCLUSION;
        }

        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_buffer(&state, &wlr_surf->buffer->base);
        wlr_presentation_surface_scanned_out_on_output(wlr_surf, output->handle);

        if (wlr_output_commit_state(output->handle, &state))
        {
            wlr_output_state_finish(&state);
            return direct_scanout::SUCCESS;
        } else
        {
            wlr_output_state_finish(&state);
            return direct_scanout::OCCLUSION;
        }
    }

    void compute_visibility(wf::output_t *output, wf::region_t& visible) override
    {
        auto our_box = self->get_bounding_box();
        on_frame_done.disconnect();
        last_visibility = visible & our_box;

        static wf::option_wrapper_t<bool> use_opaque_optimizations{
            "workarounds/enable_opaque_region_damage_optimizations"
        };

        if (!last_visibility.empty())
        {
            // We are visible on the given output => send wl_surface.frame on output frame, so that clients
            // can draw the next frame.
            output->connect(&on_frame_done);

            if (use_opaque_optimizations && self->surface)
            {
                pixman_region32_subtract(visible.to_pixman(), visible.to_pixman(),
                    &self->surface->opaque_region);
            }
        }
    }
};

void wf::scene::wlr_surface_node_t::gen_render_instances(
    std::vector<render_instance_uptr>& instances, damage_callback damage,
    wf::output_t *output)
{
    instances.push_back(std::make_unique<wlr_surface_render_instance_t>(
        std::dynamic_pointer_cast<wlr_surface_node_t>(this->shared_from_this()), damage, output));
}

wf::geometry_t wf::scene::wlr_surface_node_t::get_bounding_box()
{
    return wf::construct_box({0, 0}, current_state.size);
}

wlr_surface*wf::scene::wlr_surface_node_t::get_surface() const
{
    return this->surface;
}

std::optional<wf::texture_t> wf::scene::wlr_surface_node_t::to_texture() const
{
    if (this->current_state.current_buffer && (this->current_state.transform == WL_OUTPUT_TRANSFORM_NORMAL))
    {
        return wf::texture_t{current_state.texture, current_state.src_viewport};
    }

    return {};
}

// Idea of handling output enter/leave events: when the event comes, we store the number of enters/leaves
// for outputs and update them on the next idle. The idea is to cache together multiple events, which may
// be triggered especially when visibility recomputation happens.
void wf::scene::wlr_surface_node_t::handle_enter(wf::output_t *output)
{
    pending_visibility_delta[output]++;
    idle_update_outputs.run_once([&] () { update_pending_outputs(); });
}

void wf::scene::wlr_surface_node_t::handle_leave(wf::output_t *output)
{
    pending_visibility_delta[output]--;
    idle_update_outputs.run_once([&] () { update_pending_outputs(); });
}

void wf::scene::wlr_surface_node_t::update_pending_outputs()
{
    for (auto& [wo, delta] : pending_visibility_delta)
    {
        if (delta > 0)
        {
            visibility[wo] += delta;
            if (surface)
            {
                wlr_surface_send_enter(surface, wo->handle);
            }
        } else if (delta < 0)
        {
            if (!visibility.count(wo))
            {
                // output was destroyed, ignore.
                continue;
            }

            visibility[wo] += delta;
            if ((visibility[wo] <= 0) && surface)
            {
                wlr_surface_send_leave(surface, wo->handle);
            }

            if (visibility[wo] <= 0)
            {
                visibility.erase(wo);
            }
        }
    }

    if (surface && (visibility.size() > 0))
    {
        float max_scale = 1;
        for (auto x : visibility)
        {
            max_scale = std::max(max_scale, x.first->handle->scale);
        }

        wlr_fractional_scale_v1_notify_scale(surface, max_scale);
        wlr_surface_set_preferred_buffer_scale(surface, max_scale);
    }

    pending_visibility_delta.clear();
}
