#include "drm_fourcc.h"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/render.hpp>
#include <wayfire/opengl.hpp>
#include <random>
#include <vector>
#include <wayfire/core.hpp> // For wf::get_core()
#include <wayfire/util/log.hpp>
#include <chrono> // For time measurement
#include <glm/gtc/matrix_transform.hpp> // For glm::ortho
#include <glm/gtc/type_ptr.hpp> // For glm::value_ptr

class wayfire_test_plugin : public wf::per_output_plugin_instance_t
{
    static constexpr int buffer_size = 512;

    // OpenGL program and locations
    GLuint program_id   = 0;
    GLint transform_loc = -1;
    GLint color_loc     = -1;
    GLint tex_loc = -1;
    GLint position_loc = -1;
    GLint texcoord_loc = -1;

    // Helper function to create an OpenGL program
    GLuint create_opengl_program()
    {
        const char *vertex_shader_source =
            R"(
            #version 300 es
            layout (location = 0) in vec2 position;
            layout (location = 1) in vec2 texcoord;
            uniform mat4 transform;
            out vec2 v_texcoord;
            void main()
            {
                gl_Position = transform * vec4(position, 0.0, 1.0);
                v_texcoord = texcoord;
            }
        )";

        const char *fragment_shader_source =
            R"(
            #version 300 es
            precision mediump float;
            in vec2 v_texcoord;
            uniform sampler2D tex;
            uniform vec4 color;
            out vec4 out_color;
            void main()
            {
                out_color = color * texture(tex, v_texcoord);
            }
        )";

        return OpenGL::compile_program(vertex_shader_source, fragment_shader_source);
    }

  public:
    void init() override
    {
        program_id = create_opengl_program();
        if (program_id == 0)
        {
            LOGE("Failed to create OpenGL program!");
            // Handle error, maybe shutdown or disable the plugin
        }

        // Get uniform and attribute locations
        transform_loc = GL_CALL(glGetUniformLocation(program_id, "transform"));
        color_loc     = GL_CALL(glGetUniformLocation(program_id, "color"));
        tex_loc = GL_CALL(glGetUniformLocation(program_id, "tex"));
        position_loc = GL_CALL(glGetAttribLocation(program_id, "position"));
        texcoord_loc = GL_CALL(glGetAttribLocation(program_id, "texcoord"));

        benchmark_render_to_buffer();
        benchmark_render_to_buffer_opengl();
        wf::get_core().shutdown();
    }

    void fini() override
    {
        // Delete the OpenGL program
        if (program_id != 0)
        {
            GL_CALL(glDeleteProgram(program_id));
        }
    }

    wf::texture_t generate_random_texture(int width, int height)
    {
        wf::texture_t random_texture;

        wf::gles::run_in_context([&] ()
        {
            uint32_t format = DRM_FORMAT_ABGR8888;
            int stride = width * 4;
            std::vector<uint32_t> pixels(width * height);

            std::mt19937 rng(42);
            std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

            for (int i = 0; i < width * height; ++i)
            {
                pixels[i] = dist(rng);
            }

            wlr_texture *wlr_tex = wlr_texture_from_pixels(
                wf::get_core().renderer, format, stride, width, height, pixels.data());

            if (wlr_tex)
            {
                random_texture.texture = wlr_tex;
            } else
            {
                LOGE("Failed to create texture!");
            }
        });

        return random_texture;
    }

    void run_benchmark(GLuint fb_id, std::string name)
    {
        LOGI("Starting ", name, " render buffer benchmark...");

        wf::texture_t random_texture = generate_random_texture(buffer_size, buffer_size);
        if (!random_texture.texture)
        {
            LOGE("Failed to generate random texture!");
            return;
        }

        auto tex = wf::gles_texture_t{random_texture};
        glm::mat4 transform = glm::ortho(0.0f, (float)buffer_size, (float)buffer_size, 0.0f); // Orthographic
        glm::vec4 color     = {1.0, 1.0, 1.0, 1.0}; // White color
        auto start_time     = std::chrono::high_resolution_clock::now();

        wf::gles::run_in_context([&]
        {
            // Vertex data for a quad
            float vertices[] = {
                // positions // texture coords
                0.0f, (float)buffer_size, 0.0f, 0.0f, // bottom-left
                (float)buffer_size, (float)buffer_size, 1.0f, 0.0f, // bottom-right
                0.0f, 0.0f, 0.0f, 1.0f, // top-left

                (float)buffer_size, (float)buffer_size, 1.0f, 0.0f, // bottom-right
                (float)buffer_size, 0.0f, 1.0f, 1.0f, // top-right
                0.0f, 0.0f, 0.0f, 1.0f // top-left
            };

            GLuint VBO;
            GL_CALL(glGenBuffers(1, &VBO));
            GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, VBO));
            GL_CALL(glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW));

            for (int i = 0; i < 100000; ++i)
            {
                GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_id));
                GL_CALL(glViewport(0, 0, buffer_size, buffer_size));

                // Use the custom OpenGL program
                GL_CALL(glUseProgram(program_id));

                // Set uniform values
                GL_CALL(glUniformMatrix4fv(transform_loc, 1, GL_FALSE, glm::value_ptr(transform)));
                GL_CALL(glUniform4fv(color_loc, 1, glm::value_ptr(color)));
                GL_CALL(glActiveTexture(GL_TEXTURE0));
                GL_CALL(glBindTexture(GL_TEXTURE_2D, tex.tex_id));
                GL_CALL(glUniform1i(tex_loc, 0));

                // Set attribute pointers
                GL_CALL(glEnableVertexAttribArray(position_loc));
                GL_CALL(glVertexAttribPointer(position_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                    (void*)0));
                GL_CALL(glEnableVertexAttribArray(texcoord_loc));
                GL_CALL(glVertexAttribPointer(texcoord_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                    (void*)(2 * sizeof(float))));

                // Draw the quad
                GL_CALL(glDrawArrays(GL_TRIANGLES, 0, 6));

                // Unbind
                GL_CALL(glDisableVertexAttribArray(position_loc));
                GL_CALL(glDisableVertexAttribArray(texcoord_loc));
                GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
                GL_CALL(glUseProgram(0));
                GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0)); // Unbind the FBO
            }

            // Delete the VBO
            GL_CALL(glDeleteBuffers(1, &VBO));
            GL_CALL(glFinish());
        });

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;

        LOGI("Render ", name, " render buffer benchmark finished in ", elapsed.count(), " seconds.");
    }

    void benchmark_render_to_buffer()
    {
        auto renderer = wf::get_core().renderer;
        auto supported_render_formats =
            wlr_renderer_get_texture_formats(wf::get_core().renderer, renderer->render_buffer_caps);

        const uint32_t drm_fmt = DRM_FORMAT_ABGR8888;
        auto format = wlr_drm_format_set_get(supported_render_formats, drm_fmt);
        if (!format)
        {
            LOGE("Failed to find supported render format!");
            return;
        }

        auto buffer = wlr_allocator_create_buffer(wf::get_core().allocator, buffer_size, buffer_size, format);

        if (!buffer)
        {
            LOGE("Failed to allocate auxilliary buffer!");
            return;
        }

        int fb_id = wlr_gles2_renderer_get_buffer_fbo(renderer, buffer);

        return run_benchmark(fb_id, "aux");
    }

    void benchmark_render_to_buffer_opengl()
    {
        GLuint fbo     = 0;
        GLuint texture = 0;

        wf::gles::run_in_context([&] ()
        {
            GL_CALL(glGenTextures(1, &texture));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, texture));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, buffer_size, buffer_size, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, NULL));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            GL_CALL(glGenFramebuffers(1, &fbo));
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
            GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0));
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                LOGE("OpenGL framebuffer is not complete!");
                GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
                GL_CALL(glDeleteFramebuffers(1, &fbo));
                GL_CALL(glDeleteTextures(1, &texture));
                return;
            }
        });

        run_benchmark(fbo, "opengl");
    }
};

// Declare the plugin
DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_test_plugin>);
