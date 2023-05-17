#include "blur.hpp"

static const char *kawase_vertex_shader =
    R"(
#version 100
attribute mediump vec2 position;

varying mediump vec2 uv;

void main() {
    gl_Position = vec4(position.xy, 0.0, 1.0);
    uv = (position.xy + vec2(1.0, 1.0)) / 2.0;
})";

static const char *kawase_fragment_shader_down =
    R"(
#version 100
precision mediump float;

uniform float offset;
uniform vec2 halfpixel;
uniform sampler2D bg_texture;

varying mediump vec2 uv;

void main()
{
    vec4 sum = texture2D(bg_texture, uv) * 4.0;
    sum += texture2D(bg_texture, uv - halfpixel.xy * offset);
    sum += texture2D(bg_texture, uv + halfpixel.xy * offset);
    sum += texture2D(bg_texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset);
    sum += texture2D(bg_texture, uv - vec2(halfpixel.x, -halfpixel.y) * offset);
    gl_FragColor = sum / 8.0;
})";

static const char *kawase_fragment_shader_up =
    R"(
#version 100
precision mediump float;

uniform float offset;
uniform vec2 halfpixel;
uniform sampler2D bg_texture;

varying mediump vec2 uv;

void main()
{
    vec4 sum = texture2D(bg_texture, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);
    sum += texture2D(bg_texture, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture2D(bg_texture, uv + vec2(0.0, halfpixel.y * 2.0) * offset);
    sum += texture2D(bg_texture, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture2D(bg_texture, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);
    sum += texture2D(bg_texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;
    sum += texture2D(bg_texture, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);
    sum += texture2D(bg_texture, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;
    gl_FragColor = sum / 12.0;
})";

class wf_kawase_blur : public wf_blur_base
{
  public:
    wf_kawase_blur() : wf_blur_base("kawase")
    {
        OpenGL::render_begin();
        program[0].set_simple(OpenGL::compile_program(kawase_vertex_shader,
            kawase_fragment_shader_down));
        program[1].set_simple(OpenGL::compile_program(kawase_vertex_shader,
            kawase_fragment_shader_up));
        OpenGL::render_end();
    }

    int blur_fb0(const wf::region_t& blur_region, int width, int height) override
    {
        int iterations = iterations_opt;
        float offset = offset_opt;
        int sampleWidth, sampleHeight;

        /* Upload data to shader */
        static const float vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f, 1.0f,
            -1.0f, 1.0f
        };

        OpenGL::render_begin();
        program[0].use(wf::TEXTURE_TYPE_RGBA);

        /* Downsample */
        program[0].attrib_pointer("position", 2, 0, vertexData);
        /* Disable blending, because we may have transparent background, which
         * we want to render on uncleared framebuffer */
        GL_CALL(glDisable(GL_BLEND));
        program[0].uniform1f("offset", offset);

        for (int i = 0; i < iterations; i++)
        {
            sampleWidth  = width / (1 << i);
            sampleHeight = height / (1 << i);

            auto region = blur_region * (1.0 / (1 << i));

            program[0].uniform2f("halfpixel",
                0.5f / sampleWidth, 0.5f / sampleHeight);
            render_iteration(region, fb[i % 2], fb[1 - i % 2], sampleWidth,
                sampleHeight);
        }

        program[0].deactivate();

        /* Upsample */
        program[1].use(wf::TEXTURE_TYPE_RGBA);
        program[1].attrib_pointer("position", 2, 0, vertexData);
        program[1].uniform1f("offset", offset);
        for (int i = iterations - 1; i >= 0; i--)
        {
            sampleWidth  = width / (1 << i);
            sampleHeight = height / (1 << i);

            auto region = blur_region * (1.0 / (1 << i));

            program[1].uniform2f("halfpixel",
                0.5f / sampleWidth, 0.5f / sampleHeight);
            render_iteration(region, fb[1 - i % 2], fb[i % 2], sampleWidth,
                sampleHeight);
        }

        /* Reset gl state */
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

        program[1].deactivate();
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        OpenGL::render_end();

        return 0;
    }

    int calculate_blur_radius() override
    {
        return pow(2, iterations_opt + 1) * offset_opt * degrade_opt;
    }
};

std::unique_ptr<wf_blur_base> create_kawase_blur()
{
    return std::make_unique<wf_kawase_blur>();
}
