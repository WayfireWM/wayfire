#include "blur.hpp"

static const char *gaussian_vertex_shader =
    R"(
#version 100

attribute mediump vec2 position;
uniform vec2 size;
uniform float offset;

varying highp vec2 blurcoord[5];

void main() {
    gl_Position = vec4(position.xy, 0.0, 1.0);

    vec2 texcoord = (position.xy + vec2(1.0, 1.0)) / 2.0;

    blurcoord[0] = texcoord;
    blurcoord[1] = texcoord + vec2(1.5 * offset) / size;
    blurcoord[2] = texcoord - vec2(1.5 * offset) / size;
    blurcoord[3] = texcoord + vec2(3.5 * offset) / size;
    blurcoord[4] = texcoord - vec2(3.5 * offset) / size;
}
)";

static const char *gaussian_fragment_shader_horz =
    R"(
#version 100
precision mediump float;

uniform sampler2D bg_texture;
uniform int mode;

varying highp vec2 blurcoord[5];

void main()
{
    vec2 uv = blurcoord[0];
    vec4 bp = vec4(0.0);
    bp += texture2D(bg_texture, vec2(blurcoord[0].x, uv.y)) * 0.204164;
    bp += texture2D(bg_texture, vec2(blurcoord[1].x, uv.y)) * 0.304005;
    bp += texture2D(bg_texture, vec2(blurcoord[2].x, uv.y)) * 0.304005;
    bp += texture2D(bg_texture, vec2(blurcoord[3].x, uv.y)) * 0.093913;
    bp += texture2D(bg_texture, vec2(blurcoord[4].x, uv.y)) * 0.093913;
    gl_FragColor = bp;
})";

static const char *gaussian_fragment_shader_vert =
    R"(
#version 100
precision mediump float;

uniform sampler2D bg_texture;
uniform int mode;

varying highp vec2 blurcoord[5];

void main()
{
    vec2 uv = blurcoord[0];
    vec4 bp = vec4(0.0);
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[0].y)) * 0.204164;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[1].y)) * 0.304005;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[2].y)) * 0.304005;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[3].y)) * 0.093913;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[4].y)) * 0.093913;
    gl_FragColor = bp;
})";

class wf_gaussian_blur : public wf_blur_base
{
  public:
    wf_gaussian_blur() : wf_blur_base("gaussian")
    {
        OpenGL::render_begin();
        program[0].set_simple(OpenGL::compile_program(
            gaussian_vertex_shader, gaussian_fragment_shader_horz));
        program[1].set_simple(OpenGL::compile_program(
            gaussian_vertex_shader, gaussian_fragment_shader_vert));
        OpenGL::render_end();
    }

    void upload_data(int i, int width, int height)
    {
        float offset = offset_opt;
        static const float vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f, 1.0f,
            -1.0f, 1.0f
        };

        program[i].use(wf::TEXTURE_TYPE_RGBA);
        program[i].uniform2f("size", width, height);
        program[i].uniform1f("offset", offset);
        program[i].attrib_pointer("position", 2, 0, vertexData);
    }

    void blur(const wf::region_t& blur_region, int i, int width, int height)
    {
        program[i].use(wf::TEXTURE_TYPE_RGBA);
        render_iteration(blur_region, fb[i], fb[!i], width, height);
    }

    int blur_fb0(const wf::region_t& blur_region, int width, int height) override
    {
        int i, iterations = iterations_opt;

        OpenGL::render_begin();
        GL_CALL(glDisable(GL_BLEND));
        /* Enable our shader and pass some data to it. The shader
         * does gaussian blur on the background texture in two passes,
         * one horizontal and one vertical */
        upload_data(0, width, height);
        upload_data(1, width, height);

        for (i = 0; i < iterations; i++)
        {
            /* Blur horizontally */
            blur(blur_region, 0, width, height);

            /* Blur vertically */
            blur(blur_region, 1, width, height);
        }

        /* Reset gl state */
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        program[1].deactivate();
        OpenGL::render_end();

        return 0;
    }

    int calculate_blur_radius() override
    {
        return 4 * wf_blur_base::calculate_blur_radius();
    }
};

std::unique_ptr<wf_blur_base> create_gaussian_blur()
{
    return std::make_unique<wf_gaussian_blur>();
}
