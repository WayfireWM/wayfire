#include "blur.hpp"
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include <glm/ext/matrix_transform.hpp>
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
uniform mat4 background_uv_matrix;

void main() {

    gl_Position = mvp * vec4(position, 0.0, 1.0);
    uvpos[0] = uv_in;
    uvpos[1] = vec4(background_uv_matrix * vec4(uv_in - 0.5, 0.0, 1.0)).xy + 0.5;
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
    vec4 bp = texture2D(bg_texture, uvpos[1]);
    bp = vec4(saturation(bp.rgb, sat), bp.a);
    vec4 wp = get_pixel(uvpos[0]);
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

void wf_blur_base::prepare_blur(const wf::render_target_t& target_fb, const wf::region_t& damage)
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

    /* Make sure the result is always fb[0], because that's what is used in render()
     * */
    if (r != 0)
    {
        std::swap(fb[0], fb[1]);
    }

    prepared_geometry = damage_box;
}

static wf::pointf_t get_center(wf::geometry_t g)
{
    return {g.x + g.width / 2.0, g.y + g.height / 2.0};
}

void wf_blur_base::render(wf::texture_t src_tex, wlr_box src_box, const wf::region_t& damage,
    const wf::render_target_t& background_source_fb, const wf::render_target_t& target_fb)
{
    OpenGL::render_begin(target_fb);
    blend_program.use(src_tex.type);

    /* Use shader and enable vertex and texcoord data */
    static const float vertex_data_uv[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
    };

    const float vertex_data_pos[] = {
        1.0f * src_box.x, 1.0f * src_box.y + src_box.height,
        1.0f * src_box.x + src_box.width, 1.0f * src_box.y + src_box.height,
        1.0f * src_box.x + src_box.width, 1.0f * src_box.y,
        1.0f * src_box.x, 1.0f * src_box.y,
    };

    blend_program.attrib_pointer("position", 2, 0, vertex_data_pos);
    blend_program.attrib_pointer("uv_in", 2, 0, vertex_data_uv);

    // The blurred background is contained in a framebuffer with dimensions equal to the projected damage.
    // We need to calculate a mapping between the uv coordinates of the view (which may be bigger than the
    // damage) and the uv coordinates used for sampling the blurred background.

    // How it works:
    // 1. translate UV coordinates to (-0.5, -0.5) ~ (0.5, 0.5) range
    // 2. apply inverse framebuffer transform (needed because on rotated outputs, the framebuffer box includes
    // rotation).
    // 3. Scale to match the view size
    // 4. Translate to match the view
    auto view_box    = background_source_fb.framebuffer_box_from_geometry_box(src_box); // Projected view
    auto blurred_box = prepared_geometry;
    // prepared_geometry is the projected damage bounding box

    glm::mat4 fb_fix   = target_fb.transform;
    const auto scale_x = 1.0 * view_box.width / blurred_box.width;
    const auto scale_y = 1.0 * view_box.height / blurred_box.height;
    glm::mat4 scale    = glm::scale(glm::mat4(1.0), glm::vec3{scale_x, scale_y, 1.0});

    const wf::pointf_t center_view     = get_center(view_box);
    const wf::pointf_t center_prepared = get_center(blurred_box);
    const auto translate_x = 1.0 * (center_view.x - center_prepared.x) / view_box.width;
    const auto translate_y = -1.0 * (center_view.y - center_prepared.y) / view_box.height;
    glm::mat4 fix_center   = glm::translate(glm::mat4(1.0), glm::vec3{translate_x, translate_y, 0.0});
    glm::mat4 composite    = scale * fix_center * fb_fix;
    blend_program.uniformMatrix4f("background_uv_matrix", composite);

    /* Blend blurred background with window texture src_tex */
    blend_program.uniformMatrix4f("mvp", target_fb.get_orthographic_projection());
    /* XXX: core should give us the number of texture units used */
    blend_program.uniform1i("bg_texture", 1);
    blend_program.uniform1f("sat", saturation_opt);

    blend_program.set_active_texture(src_tex);
    GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, fb[0].tex));
    /* Render it to target_fb */
    target_fb.bind();

    for (const auto& box : damage)
    {
        target_fb.logic_scissor(wlr_box_from_pixman_box(box));
        GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
    }

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
