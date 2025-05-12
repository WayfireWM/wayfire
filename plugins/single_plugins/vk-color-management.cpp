#include "wayfire/nonstd/wlroots-full.hpp"
#include "wayfire/opengl.hpp"
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/util/duration.hpp>
#include <filesystem>
#include <fstream>
#include <wayfire/config-backend.hpp>

class wayfire_passthrough_screen : public wf::per_output_plugin_instance_t
{
    wlr_renderer *vk_renderer = NULL;
    wf::option_wrapper_t<std::string> icc_profile;
    wlr_color_transform *icc_color_transform = NULL;
    wlr_buffer_pass_options pass_opts;

  public:
    void init() override
    {
        if (wf::get_core().is_vulkan())
        {
            LOGE("The vk-color-management plugin is not necessary with the vulkan backend!");
            return;
        }

        output->render->add_post(&render_hook);

        auto section = wf::get_core().config_backend->get_output_section(output->handle);
        icc_profile.load_option(section->get_name() + "/icc_profile");
        icc_profile.set_callback([=] ()
        {
            reload_icc_profile();
            output->render->damage_whole_idle();
        });

        reload_icc_profile();
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
        pass_opts = {}; // Reset options
        pass_opts.color_transform = icc_color_transform;
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
            set_icc_transform(nullptr);
            output->render->rem_post(&render_hook);
        }
    }

    void set_icc_transform(wlr_color_transform *transform)
    {
        if (icc_color_transform)
        {
            wlr_color_transform_unref(icc_color_transform);
        }

        icc_color_transform = transform;
    }

    void reload_icc_profile()
    {
        if (icc_profile.value().empty())
        {
            set_icc_transform(nullptr);
            return;
        }

        auto path = std::filesystem::path{icc_profile.value()};
        if (std::filesystem::is_regular_file(path))
        {
            // Read binary file into vector<char> buffer
            std::ifstream file(icc_profile.value(), std::ios::binary);
            std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());

            auto transform = wlr_color_transform_init_linear_to_icc(buffer.data(), buffer.size());
            if (!transform)
            {
                LOGE("Failed to load ICC transform from ", icc_profile.value());
                set_icc_transform(nullptr);
                return;
            } else
            {
                LOGI("Loaded ICC transform from ", icc_profile.value(), " for output ", output->to_string());
            }

            set_icc_transform(transform);
        }
    }
};

// Declare the plugin
DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_passthrough_screen>);
