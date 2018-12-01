#include <core.hpp>
#include <render-manager.hpp>


/* The design of blur takes extra consideration due to the fact that
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
 * pixels wont be changing in this region of the scene. pre_render is
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
 * ...................................................................
 * |                                                                 |
 * |                                                                 |
 * |                                                                 |
 * |           ..................................                    |
 * |           |                                |..                  |
 * |           |                                | |                  |
 * |           |         Damage region          | |                  |
 * |           |                                | |                  |
 * |           |                                | |                  |
 * |           |                                | |                  |
 * |           |                                | |                  |
 * |           |                                | |                  |
 * |           |                                | |                  |
 * |           |                                | |                  |
 * |           |                                | |                  |
 * |           |                                | |                  |
 * |           ```|```````````````````````````````|                  |
 * |              `````````````````````````````````                  |
 * |                                                                 |
 * |                                                                 |
 * |                                                                 |
 * ```````````````````````````````````````````````````````````````````
 * 
 * 2)
 * ...................................................................
 * |                                                                 |
 * |                                                                 |
 * |         ......................................                  |
 * |         | .................................. |..<-- Padding     |
 * |         | |                                |.. |                |
 * |         | |                                | | |                |
 * |         | |            Padded              | | |                |
 * |         | |         Damage region          | | |                |
 * |         | |                                | | |                |
 * |         | |                                | | |                |
 * |         | |                                | | |                |
 * |         | |                                | | |                |
 * |         | |                                | | |                |
 * |         | |                                | | |                |
 * |         | |                                | | |                |
 * |         | |                                | | |                |
 * |         | ```|```````````````````````````````| |                |
 * |         ```| ````````````````````````````````` |<-- Padding     |
 * |            `````````````````````````````````````                |
 * |                                                                 |
 * |                                                                 |
 * ```````````````````````````````````````````````````````````````````
 * 
 * 3)
 * ...................................................................
 * |                                                                 |
 * |       x1|                                      |x2              |
 * |   y1__  ...................................... .                |
 * |         |                                    |..                |
 * |         |                                      |                |
 * |         |                                      |                |
 * |         |                                      |                |
 * |         |                                      |                |
 * |         |                   ^                  |                |
 * |         |                                      |                |
 * |         |         <- Padded extents ->         |                |
 * |         |                                      |                |
 * |         |                   v                  |                |
 * |         |                                      |                |
 * |         |                                      |                |
 * |         |                                      |                |
 * |         |                                      |                |
 * |   y2__  ```|                                   |                |
 * |         `  `````````````````````````````````````                |
 * |                                                                 |
 * |                                                                 |
 * ```````````````````````````````````````````````````````````````````
 */

struct blur_options
{
    float offset;
    int iterations, degrade;
};

class wayfire_box_blur
{
    wf_framebuffer_base fb[2];

    public:
    void init();
    void fini();
    void pre_render(uint32_t src_tex,
                    wlr_box src_box,
                    pixman_region32_t *damage,
                    const wf_framebuffer& target_fb,
                    struct blur_options *options);

    void render(uint32_t src_tex,
                wlr_box src_box,
                wlr_box scissor_box,
                const wf_framebuffer& target_fb);
};

class wayfire_gaussian_blur
{
    wf_framebuffer_base fb[2];

    public:
    void init();
    void fini();
    void pre_render(uint32_t src_tex,
                    wlr_box src_box,
                    pixman_region32_t *damage,
                    const wf_framebuffer& target_fb,
                    struct blur_options *options);

    void render(uint32_t src_tex,
                wlr_box src_box,
                wlr_box scissor_box,
                const wf_framebuffer& target_fb);
};

class wayfire_kawase_blur
{
    wf_framebuffer_base fb[2];

    public:
    void init();
    void fini();
    void pre_render(uint32_t src_tex,
                    wlr_box src_box,
                    pixman_region32_t *damage,
                    const wf_framebuffer& target_fb,
                    struct blur_options *options);

    void render(uint32_t src_tex,
                wlr_box src_box,
                wlr_box scissor_box,
                const wf_framebuffer& target_fb);
};

class wayfire_bokeh_blur
{
    wf_framebuffer_base fb[2];

    public:
    void init();
    void fini();
    void pre_render(uint32_t src_tex,
                    wlr_box src_box,
                    pixman_region32_t *damage,
                    const wf_framebuffer& target_fb,
                    struct blur_options *options);

    void render(uint32_t src_tex,
                wlr_box src_box,
                wlr_box scissor_box,
                const wf_framebuffer& target_fb);
};
