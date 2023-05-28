#include "blur.hpp"
#include "wayfire/core.hpp"
#include "wayfire/scene-render.hpp"
#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/util/log.hpp>

static const char *blur_blend_vertex_shader =
    R"(
#version 100

attribute mediump vec2 position;
attribute mediump vec2 uv_in;

varying mediump vec2 uvpos[2];

uniform mat4 mvp;
uniform mat4 background_inverse;

void main() {

    gl_Position = mvp * vec4(position, 0.0, 1.0);
    uvpos[0] = uv_in;
    uvpos[1] = vec4(background_inverse * vec4(uv_in - 0.5, 0.0, 1.0)).xy + 0.5;
})";

static const char *blur_blend_fragment_shader =
    R"(
#version 100
@builtin_ext@
precision mediump float;

@builtin@
uniform float sat;
uniform sampler2D bg_texture;

varying mediump vec2 uvpos[2];

vec3 saturation(vec3 rgb, float adjustment)
{
    // Algorithm from Chapter 16 of OpenGL Shading Language
    const vec3 w = vec3(0.2125, 0.7154, 0.0721);
    vec3 intensity = vec3(dot(rgb, w));
    return mix(intensity, rgb, adjustment);
}

void main()
{
    vec4 bp = texture2D(bg_texture, uvpos[0]);
    bp = vec4(saturation(bp.rgb, sat), bp.a);
    vec4 wp = get_pixel(uvpos[1]);
    vec4 c = clamp(4.0 * wp.a, 0.0, 1.0) * bp;
    gl_FragColor = wp + (1.0 - wp.a) * c;
})";

wf_blur_base::wf_blur_base(std::string name)
{
    this->algorithm_name = name;

    this->saturation_opt.load_option("blur/saturation");
    this->offset_opt.load_option("blur/" + algorithm_name + "_offset");
    this->degrade_opt.load_option("blur/" + algorithm_name + "_degrade");
    this->iterations_opt.load_option("blur/" + algorithm_name + "_iterations");

    this->options_changed = [=] ()
    {
        wf::scene::damage_node(wf::get_core().scene(), wf::get_core().scene()->get_bounding_box());
    };
    this->saturation_opt.set_callback(options_changed);
    this->offset_opt.set_callback(options_changed);
    this->degrade_opt.set_callback(options_changed);
    this->iterations_opt.set_callback(options_changed);

    OpenGL::render_begin();
    blend_program.compile(blur_blend_vertex_shader, blur_blend_fragment_shader);
    OpenGL::render_end();
}

wf_blur_base::~wf_blur_base()
{
    OpenGL::render_begin();
    fb[0].release();
    fb[1].release();
    program[0].free_resources();
    program[1].free_resources();
    blend_program.free_resources();
    OpenGL::render_end();
}

int wf_blur_base::calculate_blur_radius()
{
    return offset_opt * degrade_opt * std::max(1, (int)iterations_opt);
}

void wf_blur_base::render_iteration(wf::region_t blur_region,
    wf::framebuffer_t& in, wf::framebuffer_t& out,
    int width, int height)
{
    /* Special case for small regions where we can't really blur, because we
     * simply have too few pixels */
    width  = std::max(width, 1);
    height = std::max(height, 1);

    out.allocate(width, height);
    out.bind();

    GL_CALL(glBindTexture(GL_TEXTURE_2D, in.tex));
    for (auto& b : blur_region)
    {
        out.scissor(wlr_box_from_pixman_box(b));
        GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
    }
}

/** @return Smallest integer >= x which is divisible by mod */
static int round_up(int x, int mod)
{
    return mod * int((x + mod - 1) / mod);
}

/**
 * Calculate the smallest box which contains @box and whose x, y, width, height
 * are divisible by @degrade, and clamp that box to @bounds.
 */
static wf::geometry_t sanitize(wf::geometry_t box, int degrade,
    wf::geometry_t bounds)
{
    wf::geometry_t out_box;
    out_box.x     = degrade * int(box.x / degrade);
    out_box.y     = degrade * int(box.y / degrade);
    out_box.width = round_up(box.width, degrade);
    out_box.height = round_up(box.height, degrade);

    if (out_box.x + out_box.width < box.x + box.width)
    {
        out_box.width += degrade;
    }

    if (out_box.y + out_box.height < box.y + box.height)
    {
        out_box.height += degrade;
    }

    return wf::clamp(out_box, bounds);
}

wlr_box wf_blur_base::copy_region(wf::framebuffer_t& result,
    const wf::render_target_t& source, const wf::region_t& region)
{
    auto subbox = source.framebuffer_box_from_geometry_box(
        wlr_box_from_pixman_box(region.get_extents()));

    auto source_box =
        source.framebuffer_box_from_geometry_box(source.geometry);

    // Make sure that the box is aligned properly for degrading, otherwise,
    // we get a flickering
    subbox = sanitize(subbox, degrade_opt, source_box);
    int degraded_width  = subbox.width / degrade_opt;
    int degraded_height = subbox.height / degrade_opt;

    OpenGL::render_begin(source);
    result.allocate(degraded_width, degraded_height);

    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, source.fb));
    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, result.fb));
    GL_CALL(glBlitFramebuffer(
        subbox.x, source.viewport_height - subbox.y - subbox.height,
        subbox.x + subbox.width, source.viewport_height - subbox.y,
        0, 0, degraded_width, degraded_height,
        GL_COLOR_BUFFER_BIT, GL_LINEAR));
    OpenGL::render_end();

    return subbox;
}

void wf_blur_base::pre_render(wlr_box src_box,
    const wf::region_t& damage, const wf::render_target_t& target_fb)
{
    if (damage.empty())
    {
        return;
    }

    int degrade     = degrade_opt;
    auto damage_box = copy_region(fb[0], target_fb, damage);

    /* As an optimization, we create a region that blur can use
     * to perform minimal rendering required to blur. We start
     * by translating the input damage region */
    wf::region_t blur_damage;
    for (auto b : damage)
    {
        blur_damage |= target_fb.framebuffer_box_from_geometry_box(
            wlr_box_from_pixman_box(b));
    }

    /* Scale and translate the region */
    blur_damage += -wf::point_t{damage_box.x, damage_box.y};
    blur_damage *= 1.0 / degrade;

    int r = blur_fb0(blur_damage, fb[0].viewport_width, fb[0].viewport_height);

    /* Make sure the result is always fb[1], because that's what is used in render()
     * */
    if (r != 0)
    {
        std::swap(fb[0], fb[1]);
    }

    /* we subtract target_fb's position to so that
     * view box is relative to framebuffer */
    auto view_box = target_fb.framebuffer_box_from_geometry_box(src_box);

    OpenGL::render_begin();
    fb[1].allocate(view_box.width, view_box.height);
    fb[1].bind();
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[0].fb));

    /* Blit the blurred texture into an fb which has the size of the view,
     * so that the view texture and the blurred background can be combined
     * together in render()
     *
     * local_geometry is damage_box relative to view box */
    wlr_box local_box = damage_box + wf::point_t{-view_box.x, -view_box.y};
    GL_CALL(glBlitFramebuffer(0, 0, fb[0].viewport_width, fb[0].viewport_height,
        local_box.x,
        view_box.height - local_box.y - local_box.height,
        local_box.x + local_box.width,
        view_box.height - local_box.y,
        GL_COLOR_BUFFER_BIT, GL_LINEAR));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    OpenGL::render_end();
}

void wf_blur_base::render(wf::texture_t src_tex, wlr_box src_box,
    wlr_box scissor_box, const wf::render_target_t& target_fb)
{
    OpenGL::render_begin(target_fb);
    blend_program.use(src_tex.type);

    /* Use shader and enable vertex and texcoord data */
    static const float vertex_data_uv[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        1.0f, 0.0f,
        0.0f, 0.0f,
    };

    const float vertex_data_pos[] = {
        1.0f * src_box.x, 1.0f * src_box.y + src_box.height,
        1.0f * src_box.x + src_box.width, 1.0f * src_box.y + src_box.height,
        1.0f * src_box.x + src_box.width, 1.0f * src_box.y,
        1.0f * src_box.x, 1.0f * src_box.y,
    };

    blend_program.attrib_pointer("position", 2, 0, vertex_data_pos);
    blend_program.attrib_pointer("uv_in", 2, 0, vertex_data_uv);

    /* Blend blurred background with window texture src_tex */
    blend_program.uniformMatrix4f("background_inverse", glm::inverse(target_fb.transform));
    blend_program.uniformMatrix4f("mvp", target_fb.get_orthographic_projection());
    /* XXX: core should give us the number of texture units used */
    blend_program.uniform1i("bg_texture", 1);
    blend_program.uniform1f("sat", saturation_opt);

    blend_program.set_active_texture(src_tex);
    GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, fb[1].tex));
    /* Render it to target_fb */
    target_fb.bind();

    target_fb.scissor(scissor_box);
    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

    /*
     * Disable stuff
     * GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
     */
    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    blend_program.deactivate();
    OpenGL::render_end();
}

std::unique_ptr<wf_blur_base> create_blur_from_name(std::string algorithm_name)
{
    if (algorithm_name == "box")
    {
        return create_box_blur();
    }

    if (algorithm_name == "bokeh")
    {
        return create_bokeh_blur();
    }

    if (algorithm_name == "kawase")
    {
        return create_kawase_blur();
    }

    if (algorithm_name == "gaussian")
    {
        return create_gaussian_blur();
    }

    LOGE("Unrecognized blur algorithm %s. Using default kawase blur.", algorithm_name.c_str());
    return create_kawase_blur();
}
