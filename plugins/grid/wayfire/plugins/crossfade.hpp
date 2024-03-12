#pragma once

#include <wayfire/plugins/common/util.hpp>
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include <memory>
#include <wayfire/view-transform.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/plugins/wobbly/wobbly-signal.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/window-manager.hpp>

namespace wf
{
namespace grid
{
/**
 * A transformer used for a simple crossfade + scale animation.
 *
 * It fades out the scaled contents from original_buffer, and fades in the
 * current contents of the view, based on the alpha value in the transformer.
 */
class crossfade_node_t : public scene::view_2d_transformer_t
{
  public:
    wayfire_view view;
    // The contents of the view before the change.
    wf::render_target_t original_buffer;

  public:
    wf::geometry_t displayed_geometry;
    double overlay_alpha;

    crossfade_node_t(wayfire_toplevel_view view) : view_2d_transformer_t(view)
    {
        displayed_geometry = view->get_geometry();
        this->view = view;

        auto root_node = view->get_surface_root_node();
        const wf::geometry_t bbox = root_node->get_bounding_box();

        original_buffer.geometry = view->get_geometry();
        original_buffer.scale    = view->get_output()->handle->scale;

        OpenGL::render_begin();
        auto w = original_buffer.scale * original_buffer.geometry.width;
        auto h = original_buffer.scale * original_buffer.geometry.height;
        original_buffer.allocate(w, h);
        OpenGL::render_end();

        std::vector<scene::render_instance_uptr> instances;
        root_node->gen_render_instances(instances, [] (auto) {}, view->get_output());

        scene::render_pass_params_t params;
        params.background_color = {0, 0, 0, 0};
        params.damage    = bbox;
        params.target    = original_buffer;
        params.instances = &instances;
        scene::run_render_pass(params, scene::RPASS_CLEAR_BACKGROUND);
    }

    ~crossfade_node_t()
    {
        OpenGL::render_begin();
        original_buffer.release();
        OpenGL::render_end();
    }

    std::string stringify() const override
    {
        return "crossfade";
    }

    void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
        scene::damage_callback push_damage, wf::output_t *shown_on) override;
};

class crossfade_render_instance_t : public scene::render_instance_t
{
    crossfade_node_t *self;
    wf::signal::connection_t<scene::node_damage_signal> on_damage;

  public:
    crossfade_render_instance_t(crossfade_node_t *self,
        scene::damage_callback push_damage)
    {
        this->self = self;
        scene::damage_callback push_damage_child = [=] (const wf::region_t&)
        {
            // XXX: we could attempt to calculate a meaningful damage, but
            // we update on each frame anyway so ..
            push_damage(self->get_bounding_box());
        };

        on_damage = [=] (auto)
        {
            push_damage(self->get_bounding_box());
        };
        self->connect(&on_damage);
    }

    void schedule_instructions(
        std::vector<scene::render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        instructions.push_back(wf::scene::render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = damage & self->get_bounding_box(),
                });
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region) override
    {
        double ra;
        const double N = 2;
        if (self->overlay_alpha < 0.5)
        {
            ra = std::pow(self->overlay_alpha * 2, 1.0 / N) / 2.0;
        } else
        {
            ra = std::pow((self->overlay_alpha - 0.5) * 2, N) / 2.0 + 0.5;
        }

        OpenGL::render_begin(target);
        for (auto& box : region)
        {
            target.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::render_texture({self->original_buffer.tex}, target,
                self->displayed_geometry, glm::vec4{1.0f, 1.0f, 1.0f, 1.0 - ra});
        }

        OpenGL::render_end();
    }
};

inline void crossfade_node_t::gen_render_instances(
    std::vector<scene::render_instance_uptr>& instances,
    scene::damage_callback push_damage, wf::output_t *shown_on)
{
    // Step 2: render overlay (instances are sorted front-to-back)
    instances.push_back(
        std::make_unique<crossfade_render_instance_t>(this, push_damage));

    // Step 1: render the scaled view
    scene::view_2d_transformer_t::gen_render_instances(
        instances, push_damage, shown_on);
}

/**
 * A class used for crossfade/wobbly animation of a change in a view's geometry.
 */
class grid_animation_t : public wf::custom_data_t
{
  public:
    enum type_t
    {
        CROSSFADE,
        WOBBLY,
        NONE,
    };

    /**
     * Create an animation object for the given view.
     *
     * @param type Indicates which animation method to use.
     * @param duration Indicates the duration of the animation (only for crossfade)
     */
    grid_animation_t(wayfire_toplevel_view view, type_t type,
        wf::option_sptr_t<wf::animation_description_t> duration)
    {
        this->view   = view;
        this->output = view->get_output();
        this->type   = type;
        this->animation = wf::geometry_animation_t{duration};

        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        output->connect(&on_disappear);
    }

    /**
     * Set the view geometry and start animating towards that target using the
     * animation type.
     *
     * @param geometry The target geometry.
     * @param target_edges The tiled edges the view should have at the end of the
     *   animation. If target_edges are -1, then the tiled edges of the view will
     *   not be changed.
     */
    void adjust_target_geometry(wf::geometry_t geometry, int32_t target_edges, wf::txn::transaction_uptr& tx)
    {
        // Apply the desired attributes to the view
        const auto& set_state = [&] ()
        {
            if (target_edges >= 0)
            {
                wf::get_core().default_wm->update_last_windowed_geometry(view);
                view->toplevel()->pending().fullscreen  = false;
                view->toplevel()->pending().tiled_edges = target_edges;
            }

            view->toplevel()->pending().geometry = geometry;
            tx->add_object(view->toplevel());
        };

        if (type != CROSSFADE)
        {
            /* Order is important here: first we set the view geometry, and
             * after that we set the snap request. Otherwise the wobbly plugin
             * will think the view actually moved */
            set_state();
            if (type == WOBBLY)
            {
                activate_wobbly(view);
            }

            return destroy();
        }

        // Crossfade animation
        original = view->get_geometry();
        animation.set_start(original);
        animation.set_end(geometry);
        animation.start();

        // Add crossfade transformer
        ensure_view_transformer<crossfade_node_t>(
            view, wf::TRANSFORMER_2D, view);

        // Start the transition
        set_state();
    }

    void adjust_target_geometry(wf::geometry_t geometry, int32_t target_edges)
    {
        auto tx = wf::txn::transaction_t::create();
        adjust_target_geometry(geometry, target_edges, tx);
        wf::get_core().tx_manager->schedule_transaction(std::move(tx));
    }

    ~grid_animation_t()
    {
        view->get_transformed_node()->rem_transformer<crossfade_node_t>();
        output->render->rem_effect(&pre_hook);
    }

    grid_animation_t(const grid_animation_t &) = delete;
    grid_animation_t(grid_animation_t &&) = delete;
    grid_animation_t& operator =(const grid_animation_t&) = delete;
    grid_animation_t& operator =(grid_animation_t&&) = delete;

  protected:
    wf::effect_hook_t pre_hook = [=] ()
    {
        if (!animation.running())
        {
            return destroy();
        }

        if (view->get_geometry() != original)
        {
            original = view->get_geometry();
            animation.set_end(original);
        }

        auto tr = view->get_transformed_node()->get_transformer<crossfade_node_t>();
        view->get_transformed_node()->begin_transform_update();
        tr->displayed_geometry = animation;

        auto geometry = view->get_geometry();
        tr->scale_x = animation.width / geometry.width;
        tr->scale_y = animation.height / geometry.height;

        tr->translation_x = (animation.x + animation.width / 2) -
            (geometry.x + geometry.width / 2.0);
        tr->translation_y = (animation.y + animation.height / 2) -
            (geometry.y + geometry.height / 2.0);

        tr->overlay_alpha = animation.progress();
        view->get_transformed_node()->end_transform_update();
    };

    void destroy()
    {
        view->erase_data<grid_animation_t>();
    }

    wf::geometry_t original;
    wayfire_toplevel_view view;
    wf::output_t *output;
    wf::signal::connection_t<view_disappeared_signal> on_disappear = [=] (view_disappeared_signal *ev)
    {
        if (ev->view == view)
        {
            destroy();
        }
    };

    wf::geometry_animation_t animation;
    type_t type;
};
}
}
