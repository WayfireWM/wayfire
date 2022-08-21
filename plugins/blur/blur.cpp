#include <wayfire/plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

#include "blur.hpp"

using blur_algorithm_provider = std::function<nonstd::observer_ptr<wf_blur_base>()>;
class wf_blur_transformer : public wf::view_transformer_t
{
    blur_algorithm_provider provider;
    wf::output_t *output;
    wayfire_view view;

  public:
    wf_blur_transformer(blur_algorithm_provider blur_algorithm_provider,
        wf::output_t *output, wayfire_view view)
    {
        provider     = blur_algorithm_provider;
        this->output = output;
        this->view   = view;
    }

    wf::pointf_t transform_point(wf::geometry_t view,
        wf::pointf_t point) override
    {
        return point;
    }

    wf::pointf_t untransform_point(wf::geometry_t view,
        wf::pointf_t point) override
    {
        return point;
    }

    wlr_box get_bounding_box(wf::geometry_t view, wlr_box region) override
    {
        return region;
    }

    wf::region_t transform_opaque_region(
        wf::geometry_t bbox, wf::region_t region) override
    {
        return region;
    }

    uint32_t get_z_order() override
    {
        return wf::TRANSFORMER_BLUR;
    }

    /* Render without blending */
    void direct_render(wf::texture_t src_tex, wlr_box src_box,
        const wf::region_t& damage, const wf::render_target_t& target_fb)
    {
        OpenGL::render_begin(target_fb);
        for (auto& rect : damage)
        {
            target_fb.logic_scissor(wlr_box_from_pixman_box(rect));
            OpenGL::render_texture(src_tex, target_fb, src_box);
        }

        OpenGL::render_end();
    }

    void render_with_damage(wf::texture_t src_tex, wlr_box src_box,
        const wf::region_t& damage, const wf::render_target_t& target_fb) override
    {
        wf::region_t clip_damage = damage & src_box;

        /* We want to check if the opaque region completely occludes
         * the bounding box. If this is the case, we can skip blurring
         * altogether and just render the surface. First we disable
         * shrinking and get the opaque region without padding */
        wf::surface_interface_t::set_opaque_shrink_constraint("blur", 0);
        wf::region_t full_opaque = view->get_transformed_opaque_region();

        /* Shrink the opaque region by the padding amount since the render
         * chain expects this, as we have applied padding to damage in
         * frame_pre_paint for this frame already */
        int padding = std::ceil(provider()->calculate_blur_radius() /
            output->render->get_target_framebuffer().scale);
        wf::surface_interface_t::set_opaque_shrink_constraint("blur", padding);

        wf::region_t bbox_region{src_box};
        if ((bbox_region ^ full_opaque).empty())
        {
            /* In case the whole surface is opaque, we can simply skip blurring */
            direct_render(src_tex, src_box, damage, target_fb);

            return;
        }

        wf::region_t opaque_region  = view->get_transformed_opaque_region();
        wf::region_t blurred_region = clip_damage ^ opaque_region;

        if (!blurred_region.empty())
        {
            provider()->pre_render(src_tex, src_box, blurred_region, target_fb);
            wf::view_transformer_t::render_with_damage(src_tex, src_box,
                blurred_region, target_fb);
        }

        /* Opaque non-blurred regions can be rendered directly without blending */
        wf::region_t unblurred{opaque_region & clip_damage};
        if (!unblurred.empty())
        {
            direct_render(src_tex, src_box, unblurred, target_fb);
        }
    }

    void render_box(wf::texture_t src_tex, wlr_box src_box, wlr_box scissor_box,
        const wf::render_target_t& target_fb) override
    {
        provider()->render(src_tex, src_box, scissor_box, target_fb);
    }
};

class wayfire_blur : public wf::plugin_interface_t
{
    wf::button_callback button_toggle;

    wf::effect_hook_t frame_pre_paint;
    wf::signal_connection_t workspace_stream_pre, workspace_stream_post,
        view_attached, view_detached;

    wf::view_matcher_t blur_by_default{"blur/blur_by_default"};
    wf::option_wrapper_t<std::string> method_opt{"blur/method"};
    wf::option_wrapper_t<wf::buttonbinding_t> toggle_button{"blur/toggle"};
    wf::config::option_base_t::updated_callback_t blur_method_changed;
    std::unique_ptr<wf_blur_base> blur_algorithm;

    const std::string transformer_name = "blur";

    /* the pixels from padded_region */
    wf::framebuffer_base_t saved_pixels;
    wf::region_t padded_region;

    void add_transformer(wayfire_view view)
    {
        if (view->get_transformer(transformer_name))
        {
            return;
        }

        view->add_transformer(std::make_unique<wf_blur_transformer>(
            [=] () {return nonstd::make_observer(blur_algorithm.get()); },
            output, view),
            transformer_name);
    }

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformer(transformer_name))
        {
            view->pop_transformer(transformer_name);
        }
    }

    void remove_transformers()
    {
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            pop_transformer(view);
        }
    }

    /** Transform region into framebuffer coordinates */
    wf::region_t get_fb_region(const wf::region_t& region,
        const wf::render_target_t& fb) const
    {
        wf::region_t result;
        for (const auto& rect : region)
        {
            result |= fb.framebuffer_box_from_geometry_box(
                wlr_box_from_pixman_box(rect));
        }

        return result;
    }

    wf::region_t expand_region(const wf::region_t& region, double scale) const
    {
        // As long as the padding is big enough to cover the
        // furthest sampled pixel by the shader, there should
        // be no visual artifacts.
        int padding = std::ceil(
            blur_algorithm->calculate_blur_radius() / scale);

        wf::region_t padded;
        for (const auto& rect : region)
        {
            padded |= wlr_box{
                (rect.x1 - padding),
                (rect.y1 - padding),
                (rect.x2 - rect.x1) + 2 * padding,
                (rect.y2 - rect.y1) + 2 * padding
            };
        }

        return padded;
    }

    // Blur region for current frame
    wf::region_t blur_region;

    void update_blur_region()
    {
        blur_region.clear();
        auto views = output->workspace->get_views_in_layer(wf::ALL_LAYERS);

        for (auto& view : views)
        {
            if (!view->get_transformer("blur"))
            {
                continue;
            }

            auto bbox = view->get_bounding_box();
            if (!view->sticky)
            {
                blur_region |= bbox;
            } else
            {
                auto wsize = output->workspace->get_workspace_grid_size();
                for (int i = 0; i < wsize.width; i++)
                {
                    for (int j = 0; j < wsize.height; j++)
                    {
                        blur_region |=
                            bbox + wf::origin(output->render->get_ws_box({i, j}));
                    }
                }
            }
        }
    }

    /** Find the region of blurred views on the given workspace */
    wf::region_t get_blur_region(wf::point_t ws) const
    {
        return blur_region & output->render->get_ws_box(ws);
    }

  public:
    void init() override
    {
        grab_interface->name = "blur";
        grab_interface->capabilities = 0;

        blur_method_changed = [=] ()
        {
            blur_algorithm = create_blur_from_name(output, method_opt);
            output->render->damage_whole();
        };
        /* Create initial blur algorithm */
        blur_method_changed();
        method_opt.set_callback(blur_method_changed);

        /* Toggles the blur state of the view the user clicked on */
        button_toggle = [=] (auto)
        {
            if (!output->can_activate_plugin(grab_interface))
            {
                return false;
            }

            auto view = wf::get_core().get_cursor_focus_view();
            if (!view)
            {
                return false;
            }

            if (view->get_transformer(transformer_name))
            {
                view->pop_transformer(transformer_name);
            } else
            {
                add_transformer(view);
            }

            return true;
        };
        output->add_button(toggle_button, &button_toggle);

        // Add blur transformers to views which have blur enabled
        view_attached.set_callback([=] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);
            /* View was just created -> we don't know its layer yet */
            if (!view->is_mapped())
            {
                return;
            }

            if (blur_by_default.matches(view))
            {
                add_transformer(view);
            }
        });

        /* If a view is detached, we remove its blur transformer.
         * If it is just moved to another output, the blur plugin
         * on the other output will add its own transformer there */
        view_detached.set_callback([=] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);
            pop_transformer(view);
        });
        output->connect_signal("view-attached", &view_attached);
        output->connect_signal("view-mapped", &view_attached);
        output->connect_signal("view-detached", &view_detached);

        /* frame_pre_paint is called before each frame has started.
         * It expands the damage by the blur radius.
         * This is needed, because when blurring, the pixels that changed
         * affect a larger area than the really damaged region, e.g the region
         * that comes from client damage */
        frame_pre_paint = [=] ()
        {
            update_blur_region();
            auto damage    = output->render->get_scheduled_damage();
            const auto& fb = output->render->get_target_framebuffer();

            int padding = std::ceil(
                blur_algorithm->calculate_blur_radius() / fb.scale);
            wf::surface_interface_t::set_opaque_shrink_constraint("blur",
                padding);

            output->render->damage(expand_region(
                damage & this->blur_region, fb.scale));
        };
        output->render->add_effect(&frame_pre_paint, wf::OUTPUT_EFFECT_DAMAGE);

        /* workspace_stream_pre is called before rendering each frame
         * when rendering a workspace. It gives us a chance to pad
         * damage and take a snapshot of the padded area. The padded
         * damage will be used to render the scene as normal. Then
         * workspace_stream_post is called so we can copy the padded
         * pixels back. */
        workspace_stream_pre.set_callback([=] (wf::signal_data_t *data)
        {
            auto& damage   = static_cast<wf::stream_signal_t*>(data)->raw_damage;
            const auto& ws = static_cast<wf::stream_signal_t*>(data)->ws;
            const auto& target_fb = static_cast<wf::stream_signal_t*>(data)->fb;

            wf::region_t expanded_damage =
                expand_region(damage & get_blur_region(ws), target_fb.scale);

            /* Keep rects on screen */
            expanded_damage &= output->render->get_ws_box(ws);

            /* Compute padded region and store result in padded_region.
             * We need to be careful, because core needs to scale the damage
             * back and forth for wlroots. */
            padded_region = get_fb_region(expanded_damage, target_fb) ^
                get_fb_region(damage, target_fb);

            OpenGL::render_begin(target_fb);
            /* Initialize a place to store padded region pixels. */
            saved_pixels.allocate(target_fb.viewport_width,
                target_fb.viewport_height);

            /* Setup framebuffer I/O. target_fb contains the pixels
             * from last frame at this point. We are writing them
             * to saved_pixels, bound as GL_DRAW_FRAMEBUFFER */
            saved_pixels.bind();
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fb.fb));

            /* Copy pixels in padded_region from target_fb to saved_pixels. */
            for (const auto& box : padded_region)
            {
                GL_CALL(glBlitFramebuffer(
                    box.x1, target_fb.viewport_height - box.y2,
                    box.x2, target_fb.viewport_height - box.y1,
                    box.x1, box.y1, box.x2, box.y2,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR));
            }

            /* This effectively makes damage the same as expanded_damage. */
            damage |= expanded_damage;
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            OpenGL::render_end();
        });

        output->render->connect_signal("workspace-stream-pre",
            &workspace_stream_pre);

        /* workspace_stream_post is called after rendering each frame
         * when rendering a workspace. It gives us a chance to copy
         * the pixels back to the framebuffer that we saved in
         * workspace_stream_pre. */
        workspace_stream_post.set_callback([=] (wf::signal_data_t *data)
        {
            const auto& target_fb = static_cast<wf::stream_signal_t*>(data)->fb;
            OpenGL::render_begin(target_fb);
            /* Setup framebuffer I/O. target_fb contains the frame
             * rendered with expanded damage and artifacts on the edges.
             * saved_pixels has the the padded region of pixels to overwrite the
             * artifacts that blurring has left behind. */
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, saved_pixels.fb));

            /* Copy pixels back from saved_pixels to target_fb. */
            for (const auto& box : padded_region)
            {
                GL_CALL(glBlitFramebuffer(box.x1, box.y1, box.x2, box.y2,
                    box.x1, target_fb.viewport_height - box.y2,
                    box.x2, target_fb.viewport_height - box.y1,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR));
            }

            /* Reset stuff */
            padded_region.clear();
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            OpenGL::render_end();
        });

        output->render->connect_signal("workspace-stream-post",
            &workspace_stream_post);

        for (auto& view :
             output->workspace->get_views_in_layer(wf::ALL_LAYERS))
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

        output->rem_binding(&button_toggle);
        output->render->rem_effect(&frame_pre_paint);

        /* Call blur algorithm destructor */
        blur_algorithm = nullptr;

        OpenGL::render_begin();
        saved_pixels.release();
        OpenGL::render_end();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_blur);
