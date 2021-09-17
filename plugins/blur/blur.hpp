#include <wayfire/core.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>

/* The MIT License (MIT)
 *
 * Copyright (c) 2018 Ilia Bozhinov
 * Copyright (c) 2018 Scott Moreau
 *
 * The design of blur takes extra consideration due to the fact that
 * the results of blurred pixels rely on surrounding pixel values.
 * This means that when damage happens for only part of the scene (1),
 * blurring this area can result to artifacts because of sampling
 * beyond the edges of the area. To work around this issue, wayfire
 * issues two signals - workspace-stream-pre and workspace-stream-post.
 * workspace-stream-pre gives plugins an opportunity to pad the rects
 * of the damage region (2) and save a snap-shot of the padded area from
 * the buffer containing the last frame. This will be used to redraw
 * the area that will contain artifacts after rendering. This is ok
 * because this area is outside of the original damage area, so the
 * pixels won't be changing in this region of the scene. pre_render is
 * called with the padded damage region as an argument (2). The padded
 * damage extents (3) are used for blitting from the framebuffer, which
 * contains the scene rendered up until the view for which pre_render
 * is called. The padded damage extents rect is blurred with artifacts
 * in pre_render, after which it is then alpha blended with the window
 * and rendered to the framebuffer. Finally, workspace-stream-post
 * allows a chance to redraw the padded area with the saved pixels,
 * before swapping buffers. As long as the padding is enough to cover
 * the maximum sample offset that the shader uses, there should be a
 * seamless experience.
 *
 * 1)
 * .................................................................
 * |                                                               |
 * |                                                               |
 * |           ..................................                  |
 * |           |                                |..                |
 * |           |                                | |                |
 * |           |         Damage region          | |                |
 * |           |                                | |                |
 * |           |                                | |                |
 * |           |                                | |                |
 * |           |                                | |                |
 * |           ```|```````````````````````````````|                |
 * |              `````````````````````````````````                |
 * |                                                               |
 * |                                                               |
 * `````````````````````````````````````````````````````````````````
 *
 * 2)
 * .................................................................
 * |                                                               |
 * |         ......................................                |
 * |         | .................................. |..<-- Padding   |
 * |         | |                                |.. |              |
 * |         | |                                | | |              |
 * |         | |            Padded              | | |              |
 * |         | |         Damage region          | | |              |
 * |         | |                                | | |              |
 * |         | |                                | | |              |
 * |         | |                                | | |              |
 * |         | |                                | | |              |
 * |         | ```|```````````````````````````````| |              |
 * |         ```| ````````````````````````````````` |<-- Padding   |
 * |            `````````````````````````````````````              |
 * `````````````````````````````````````````````````````````````````
 *
 * 3)
 * .................................................................
 * |                                                               |
 * |       x1|                                      |x2            |
 * |   y1__  ...................................... .              |
 * |         |                                    |..              |
 * |         |                                      |              |
 * |         |                   ^                  |              |
 * |         |                                      |              |
 * |         |         <- Padded extents ->         |              |
 * |         |                                      |              |
 * |         |                   v                  |              |
 * |         |                                      |              |
 * |   y2__  ```|                                   |              |
 * |         `  `````````````````````````````````````              |
 * |                                                               |
 * `````````````````````````````````````````````````````````````````
 */

class wf_blur_base
{
  protected:
    /* used to store temporary results in blur algorithms, cleaned up in base
     * destructor */
    wf::framebuffer_base_t fb[2];
    /* the program created by the given algorithm, cleaned up in base destructor */
    OpenGL::program_t program[2];
    /* the program used by wf_blur_base to combine the blurred, unblurred and
     * view texture */
    OpenGL::program_t blend_program;

    /* used to get individual algorithm options from config
     * should be set by the constructor */
    std::string algorithm_name;

    wf::option_wrapper_t<double> saturation_opt;
    wf::option_wrapper_t<double> offset_opt;
    wf::option_wrapper_t<int> degrade_opt, iterations_opt;
    wf::config::option_base_t::updated_callback_t options_changed;

    wf::output_t *output;

    /* renders the in texture to the out framebuffer.
     * assumes a properly bound and initialized GL program */
    void render_iteration(wf::region_t blur_region,
        wf::framebuffer_base_t& in, wf::framebuffer_base_t& out,
        int width, int height);

    /* copy the source pixels from region, storing into result
     * returns the result geometry, in framebuffer coords */
    wlr_box copy_region(wf::framebuffer_base_t& result,
        const wf::framebuffer_t& source, const wf::region_t& region);

    /* blur fb[0]
     * width and height are the scaled dimensions of the buffer
     * returns the index of the fb where the result is stored (0 or 1) */
    virtual int blur_fb0(const wf::region_t& blur_region, int width, int height) = 0;

  public:
    wf_blur_base(wf::output_t *output, std::string name);
    virtual ~wf_blur_base();

    virtual int calculate_blur_radius();

    virtual void pre_render(wf::texture_t src_tex, wlr_box src_box,
        const wf::region_t& damage, const wf::framebuffer_t& target_fb);

    virtual void render(wf::texture_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf::framebuffer_t& target_fb);
};

std::unique_ptr<wf_blur_base> create_box_blur(wf::output_t *output);
std::unique_ptr<wf_blur_base> create_bokeh_blur(wf::output_t *output);
std::unique_ptr<wf_blur_base> create_kawase_blur(wf::output_t *output);
std::unique_ptr<wf_blur_base> create_gaussian_blur(wf::output_t *output);

std::unique_ptr<wf_blur_base> create_blur_from_name(wf::output_t *output,
    std::string algorithm_name);
