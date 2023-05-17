#include "blur.hpp"

static const char *bokeh_vertex_shader =
    R"(
#version 100

attribute mediump vec2 position;
varying mediump vec2 uv;

void main() {

    gl_Position = vec4(position.xy, 0.0, 1.0);
    uv = (position.xy + vec2(1.0, 1.0)) / 2.0;
}
)";

static const char *bokeh_fragment_shader =
    R"(
#version 100
precision mediump float;

uniform float offset;
uniform int iterations;
uniform vec2 halfpixel;
uniform int mode;

uniform sampler2D bg_texture;
varying mediump vec2 uv;

#define GOLDEN_ANGLE 2.39996

mat2 rot = mat2(cos(GOLDEN_ANGLE), sin(GOLDEN_ANGLE), -sin(GOLDEN_ANGLE), cos(GOLDEN_ANGLE));

void main()
{
    float radius = offset;
    vec4 acc = vec4(0), div = acc;
    float r = 1.0;
    vec2 vangle = vec2(radius / sqrt(float(iterations)), radius / sqrt(float(iterations)));
    for (int j = 0; j < iterations; j++)
    {
        r += 1.0 / r;
        vangle = rot * vangle;
        vec4 col = texture2D(bg_texture, uv + (r - 1.0) * vangle * halfpixel * 2.0);
        vec4 bokeh = pow(col, vec4(4.0));
        acc += col * bokeh;
        div += bokeh;
    }

    if (iterations == 0)
        gl_FragColor = texture2D(bg_texture, uv);
    else
        gl_FragColor = acc / div;
}
)";

class wf_bokeh_blur : public wf_blur_base
{
  public:
    wf_bokeh_blur() : wf_blur_base("bokeh")
    {
        OpenGL::render_begin();
        program[0].set_simple(OpenGL::compile_program(bokeh_vertex_shader,
            bokeh_fragment_shader));
        OpenGL::render_end();
    }

    int blur_fb0(const wf::region_t& blur_region, int width, int height) override
    {
        int iterations = iterations_opt;
        float offset   = offset_opt;

        static const float vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f, 1.0f,
            -1.0f, 1.0f
        };

        OpenGL::render_begin();
        /* Upload data to shader */
        program[0].use(wf::TEXTURE_TYPE_RGBA);
        program[0].uniform2f("halfpixel", 0.5f / width, 0.5f / height);
        program[0].uniform1f("offset", offset);
        program[0].uniform1i("iterations", iterations);

        program[0].attrib_pointer("position", 2, 0, vertexData);
        GL_CALL(glDisable(GL_BLEND));
        render_iteration(blur_region, fb[0], fb[1], width, height);

        /* Reset gl state */
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

        program[0].deactivate();
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        OpenGL::render_end();

        return 1;
    }

    int calculate_blur_radius() override
    {
        return 5 * wf_blur_base::offset_opt*wf_blur_base::degrade_opt;
    }
};

std::unique_ptr<wf_blur_base> create_bokeh_blur()
{
    return std::make_unique<wf_bokeh_blur>();
}
