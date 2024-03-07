#include <wayfire/per-output-plugin.hpp>
#include <memory>
#include <wayfire/config/types.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/bindings-repository.hpp>

#include "blur.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"

using blur_algorithm_provider =
    std::function<nonstd::observer_ptr<wf_blur_base>()>;

static int calculate_damage_padding(const wf::render_target_t& target, int blur_radius)
{
    float scale = target.scale;
    if (target.subbuffer)
    {
        const float subbox_scale_x = 1.0 * target.subbuffer->width / target.geometry.width;
        const float subbox_scale_y = 1.0 * target.subbuffer->height / target.geometry.height;
        scale *= std::max(subbox_scale_x, subbox_scale_y);
    }

    return std::ceil(blur_radius / scale);
}

namespace wf
{
namespace scene
{
class blur_node_t : public transformer_base_node_t
{
  public:
    blur_algorithm_provider provider;
    blur_node_t(blur_algorithm_provider provider) : transformer_base_node_t(false)
    {
        this->provider = provider;
    }

    std::string stringify() const override
    {
        return "blur";
    }

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override;
};

class blur_render_instance_t : public transformer_render_instance_t<blur_node_t>
{
    wf::framebuffer_t saved_pixels;
    wf::region_t saved_pixels_region;

  public:
    using transformer_render_instance_t::transformer_render_instance_t;
    ~blur_render_instance_t()
    {
        OpenGL::render_begin();
        saved_pixels.release();
        OpenGL::render_end();
    }

    bool is_fully_opaque(wf::region_t damage)
    {
        if (self->get_children().size() == 1)
        {
            if (auto opaque = dynamic_cast<opaque_region_node_t*>(self->get_children().front().get()))
            {
                return (damage ^ opaque->get_opaque_region()).empty();
            }
        }

        return false;
    }

    wf::region_t calculate_translucent_damage(const wf::render_target_t& target, wf::region_t damage)
    {
        if (self->get_children().size() == 1)
        {
            if (auto opaque = dynamic_cast<opaque_region_node_t*>(self->get_children().front().get()))
            {
                const int padding =
                    calculate_damage_padding(target, self->provider()->calculate_blur_radius());
                auto opaque_region = opaque->get_opaque_region();
                opaque_region.expand_edges(-padding);

                wf::region_t translucent_region = damage ^ opaque_region;
                return translucent_region;
            }
        }

        return damage;
    }

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        const int padding = calculate_damage_padding(target, self->provider()->calculate_blur_radius());
        auto bbox = self->get_bounding_box();

        // In order to render a part of the blurred background, we need to sample
        // from area which is larger than the damaged area. However, the edges
        // of the expanded area suffer from the same problem (e.g. the blurred
        // background has artifacts). The solution to this is to expand the
        // damage and keep a copy of the pixels where we redraw, but wouldn't
        // have redrawn if not for blur. After that, we copy those old areas
        // back to the destination framebuffer, giving the illusion that they
        // were never damaged.
        auto padded_region = damage & bbox;

        if (is_fully_opaque(padded_region & target.geometry))
        {
            // If there are no regions to blur, we can directly render them.
            for (auto& ch : this->children)
            {
                ch->schedule_instructions(instructions, target, damage);
            }

            return;
        }

        padded_region.expand_edges(padding);
        padded_region &= bbox;

        // Don't forget to keep expanded damage within the bounds of the render
        // target, otherwise we may be sampling from outside of it (undefined
        // contents).
        padded_region &= target.geometry;

        // Actual region which will be repainted by this render instance.
        wf::region_t we_repaint = padded_region;

        saved_pixels_region =
            target.framebuffer_region_from_geometry_region(padded_region) ^
            target.framebuffer_region_from_geometry_region(damage);

        // Nodes below should re-render the padded areas so that we can sample from them
        damage |= padded_region;

        OpenGL::render_begin();
        saved_pixels.allocate(target.viewport_width, target.viewport_height);
        saved_pixels.bind();
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fb));

        /* Copy pixels in padded_region from target_fb to saved_pixels. */
        for (const auto& box : saved_pixels_region)
        {
            GL_CALL(glBlitFramebuffer(
                box.x1, target.viewport_height - box.y2,
                box.x2, target.viewport_height - box.y1,
                box.x1, box.y1, box.x2, box.y2,
                GL_COLOR_BUFFER_BIT, GL_LINEAR));
        }

        OpenGL::render_end();
        instructions.push_back(render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = we_repaint,
                });
    }

    void render(const wf::render_target_t& target, const wf::region_t& damage) override
    {
        auto tex = get_texture(target.scale);
        auto bounding_box = self->get_bounding_box();
        if (!damage.empty())
        {
            auto translucent_damage = calculate_translucent_damage(target, damage);
            self->provider()->prepare_blur(target, translucent_damage);
            self->provider()->render(tex, bounding_box, damage, target, target);
        }

        OpenGL::render_begin(target);
        // Setup framebuffer I/O. target_fb contains the frame
        // rendered with expanded damage and artifacts on the edges.
        // saved_pixels has the the padded region of pixels to overwrite the
        // artifacts that blurring has left behind.
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, saved_pixels.fb));

        /* Copy pixels back from saved_pixels to target_fb. */
        for (const auto& box : saved_pixels_region)
        {
            GL_CALL(glBlitFramebuffer(
                box.x1, box.y1, box.x2, box.y2,
                box.x1, target.viewport_height - box.y2,
                box.x2, target.viewport_height - box.y1,
                GL_COLOR_BUFFER_BIT, GL_LINEAR));
        }

        /* Reset stuff */
        saved_pixels_region.clear();
        OpenGL::render_end();
    }

    direct_scanout try_scanout(wf::output_t *output) override
    {
        // Enable direct scanout if it is possible
        return scene::try_scanout_from_list(children, output);
    }
};

void blur_node_t::gen_render_instances(std::vector<render_instance_uptr>& instances,
    damage_callback push_damage, wf::output_t *shown_on)
{
    auto uptr =
        std::make_unique<blur_render_instance_t>(this, push_damage, shown_on);
    if (uptr->has_instances())
    {
        instances.push_back(std::move(uptr));
    }
}
}
}

class wayfire_blur : public wf::plugin_interface_t
{
    // Before doing a render pass, expand the damage by the blur radius.
    // This is needed, because when blurring, the pixels that changed
    // affect a larger area than the really damaged region, e.g. the region
    // that comes from client damage.
    wf::signal::connection_t<wf::scene::render_pass_begin_signal>
    on_render_pass_begin = [=] (wf::scene::render_pass_begin_signal *ev)
    {
        if (!provider)
        {
            return;
        }

        const int padding = calculate_damage_padding(ev->target, provider()->calculate_blur_radius());
        ev->damage.expand_edges(padding);
        ev->damage &= ev->target.geometry;
    };

  public:
    blur_algorithm_provider provider;
    wf::button_callback button_toggle;
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        if (blur_by_default.matches(ev->view))
        {
            add_transformer(ev->view);
        }
    };

    wf::view_matcher_t blur_by_default{"blur/blur_by_default"};
    wf::option_wrapper_t<std::string> method_opt{"blur/method"};
    wf::option_wrapper_t<wf::buttonbinding_t> toggle_button{"blur/toggle"};
    wf::config::option_base_t::updated_callback_t blur_method_changed;
    std::unique_ptr<wf_blur_base> blur_algorithm;

    void add_transformer(wayfire_view view)
    {
        auto tmanager = view->get_transformed_node();
        if (tmanager->get_transformer<wf::scene::blur_node_t>())
        {
            return;
        }

        auto provider = [=] ()
        {
            return blur_algorithm.get();
        };

        auto node = std::make_shared<wf::scene::blur_node_t>(provider);
        tmanager->add_transformer(node, wf::TRANSFORMER_BLUR);
    }

    void pop_transformer(wayfire_view view)
    {
        auto tmanager = view->get_transformed_node();
        tmanager->rem_transformer<wf::scene::blur_node_t>();
    }

    void remove_transformers()
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            pop_transformer(view);
        }
    }

  public:
    void init() override
    {
        wf::get_core().connect(&on_render_pass_begin);
        blur_method_changed = [=] ()
        {
            blur_algorithm = create_blur_from_name(method_opt);
            wf::scene::damage_node(wf::get_core().scene(), wf::get_core().scene()->get_bounding_box());
        };

        /* Create initial blur algorithm */
        blur_method_changed();
        method_opt.set_callback(blur_method_changed);

        /* Toggles the blur state of the view the user clicked on */
        button_toggle = [=] (auto)
        {
            auto view = wf::get_core().get_cursor_focus_view();
            if (!view)
            {
                return false;
            }

            if (view->get_transformed_node()->get_transformer<wf::scene::blur_node_t>())
            {
                pop_transformer(view);
            } else
            {
                add_transformer(view);
            }

            return true;
        };

        wf::get_core().bindings->add_button(toggle_button, &button_toggle);
        provider = [=] () { return this->blur_algorithm.get(); };
        wf::get_core().connect(&on_view_mapped);

        for (auto& view : wf::get_core().get_all_views())
        {
            if (blur_by_default.matches(view))
            {
                add_transformer(view);
            }
        }
    }

    void fini() override
    {
        remove_transformers();
        wf::get_core().bindings->rem_binding(&button_toggle);

        /* Call blur algorithm destructor */
        blur_algorithm = nullptr;
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_blur);
