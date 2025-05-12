#include "wayfire/nonstd/wlroots-full.hpp"
#include "wayfire/opengl.hpp"
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/config-backend.hpp>

class wayfire_passthrough_screen : public wf::per_output_plugin_instance_t
{
    wlr_renderer *vk_renderer = NULL;

  public:
    void init() override
    {
        if (wf::get_core().is_vulkan())
        {
            LOGE("The vk-color-management plugin is not necessary with the vulkan backend!");
            return;
        }

        output->render->add_post(&render_hook);

        vk_renderer = wlr_vk_renderer_create_with_drm_fd(wlr_renderer_get_drm_fd(wf::get_core().renderer));
    }

    wf::post_hook_t render_hook = [=] (wf::auxilliary_buffer_t& source,
                                       const wf::render_buffer_t& destination)
    {
        GL_CALL(glFinish());
        wlr_dmabuf_attributes dmabuf{};

        if (!wlr_buffer_get_dmabuf(source.get_buffer(), &dmabuf))
        {
            LOGE("Failed to get dmabuf!");
            return;
        }

        auto vk_tex = wlr_texture_from_dmabuf(vk_renderer, &dmabuf);
        if (!vk_tex)
        {
            LOGE("Failed to create vk texture!");
            return;
        }

        // Get the size of the destination buffer
        auto w = destination.get_size().width;
        auto h = destination.get_size().height;

        // Begin a buffer pass with the destination buffer
        wlr_buffer_pass_options pass_opts{}; // Reset options
        pass_opts.color_transform = output->render->get_color_transform();
        auto pass = wlr_renderer_begin_buffer_pass(vk_renderer, destination.get_buffer(), &pass_opts);

        // Set up options to render the source texture to the destination
        wlr_render_texture_options tex{};
        tex.texture    = vk_tex; // Use the source texture
        tex.blend_mode = WLR_RENDER_BLEND_MODE_NONE;
        // Copy the entire source buffer to the entire destination buffer
        tex.src_box     = {0.0, 0.0, (double)source.get_size().width, (double)source.get_size().height};
        tex.dst_box     = {0, 0, w, h};
        tex.filter_mode = WLR_SCALE_FILTER_BILINEAR; // Use bilinear filtering for a smooth copy
        tex.transform   = WL_OUTPUT_TRANSFORM_NORMAL;
        tex.alpha = NULL;
        tex.clip  = NULL;
        wlr_render_pass_add_texture(pass, &tex);
        wlr_render_pass_submit(pass);
        wlr_texture_destroy(vk_tex);
    };

    void fini() override
    {
        if (vk_renderer)
        {
            wlr_renderer_destroy(vk_renderer);
            output->render->rem_post(&render_hook);
        }
    }
};

// Declare the plugin
DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_passthrough_screen>);
