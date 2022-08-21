#pragma once

#include <wayfire/view-transform.hpp>
#include <wayfire/output.hpp>
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/plugins/wobbly/wobbly-signal.hpp>

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
class crossfade_t : public wf::view_2D
{
  public:
    crossfade_t(wayfire_view view) :
        wf::view_2D(view)
    {
        // Create a copy of the view contents
        original_buffer.geometry = view->get_wm_geometry();
        original_buffer.scale    = view->get_output()->handle->scale;

        auto w = original_buffer.scale * original_buffer.geometry.width;
        auto h = original_buffer.scale * original_buffer.geometry.height;

        OpenGL::render_begin();
        original_buffer.allocate(w, h);
        original_buffer.bind();
        OpenGL::clear({0, 0, 0, 0});
        OpenGL::render_end();

        auto og = view->get_output_geometry();
        for (auto& surface : view->enumerate_surfaces(wf::origin(og)))
        {
            wf::region_t damage = wf::geometry_t{
                surface.position.x,
                surface.position.y,
                surface.surface->get_size().width,
                surface.surface->get_size().height
            };

            damage &= original_buffer.geometry;
            surface.surface->simple_render(original_buffer,
                surface.position.x, surface.position.y, damage);
        }
    }

    void render_box(wf::texture_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf::render_target_t& fb) override
    {
        // See the current target geometry
        auto bbox = view->get_wm_geometry();
        bbox = this->get_bounding_box(bbox, bbox);

        double saved = this->alpha;
        this->alpha = 1.0;
        // Now render the real view
        view_2D::render_box(src_tex, src_box, scissor_box, fb);
        this->alpha = saved;

        double ra;
        const double N = 2;
        if (alpha < 0.5)
        {
            ra = std::pow(alpha * 2, 1.0 / N) / 2.0;
        } else
        {
            ra = std::pow((alpha - 0.5) * 2, N) / 2.0 + 0.5;
        }

        // First render the original buffer with corresponding alpha
        OpenGL::render_begin(fb);
        fb.logic_scissor(scissor_box);
        OpenGL::render_texture({original_buffer.tex}, fb, bbox,
            glm::vec4{1.0f, 1.0f, 1.0f, 1.0 - ra});
        OpenGL::render_end();
    }

    ~crossfade_t()
    {
        OpenGL::render_begin();
        original_buffer.release();
        OpenGL::render_end();
    }

    // The contents of the view before the change.
    wf::render_target_t original_buffer;
};

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
    grid_animation_t(wayfire_view view, type_t type,
        wf::option_sptr_t<int> duration)
    {
        this->view   = view;
        this->output = view->get_output();
        this->type   = type;
        this->animation = wf::geometry_animation_t{duration};

        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        output->connect_signal("view-disappeared", &unmapped);
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
    void adjust_target_geometry(wf::geometry_t geometry, int32_t target_edges)
    {
        // Apply the desired attributes to the view
        const auto& set_state = [=] ()
        {
            if (target_edges >= 0)
            {
                view->set_fullscreen(false);
                view->set_tiled(target_edges);
            }

            view->set_geometry(geometry);
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
        original = view->get_wm_geometry();
        animation.set_start(original);
        animation.set_end(geometry);
        animation.start();

        // Add crossfade transformer
        if (!view->get_transformer("grid-crossfade"))
        {
            view->add_transformer(
                std::make_unique<wf::grid::crossfade_t>(view),
                "grid-crossfade");
        }

        // Start the transition
        set_state();
    }

    ~grid_animation_t()
    {
        view->pop_transformer("grid-crossfade");
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

        if (view->get_wm_geometry() != original)
        {
            original = view->get_wm_geometry();
            animation.set_end(original);
        }

        view->damage();

        auto tr_untyped = view->get_transformer("grid-crossfade").get();
        auto tr = dynamic_cast<wf::grid::crossfade_t*>(tr_untyped);

        auto geometry = view->get_wm_geometry();

        tr->scale_x = animation.width / geometry.width;
        tr->scale_y = animation.height / geometry.height;

        tr->translation_x = (animation.x + animation.width / 2) -
            (geometry.x + geometry.width / 2.0);
        tr->translation_y = (animation.y + animation.height / 2) -
            (geometry.y + geometry.height / 2.0);

        tr->alpha = animation.progress();
        view->damage();
    };

    void destroy()
    {
        view->erase_data<grid_animation_t>();
    }

    wf::geometry_t original;
    wayfire_view view;
    wf::output_t *output;
    wf::signal_connection_t unmapped = [=] (auto data)
    {
        if (get_signaled_view(data) == view)
        {
            destroy();
        }
    };

    wf::geometry_animation_t animation;
    type_t type;
};
}
}
