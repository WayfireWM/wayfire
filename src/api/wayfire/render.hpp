#pragma once

#include <memory>
#include <vector>
#include <wayfire/config/types.hpp>
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/region.hpp>
#include <optional>

namespace wf
{
class output_t;

/**
 * A simple, non-owning wrapper for a wlr_texture + source box.
 */
struct texture_t
{
    wlr_texture *texture = NULL;
    std::optional<wlr_fbox> source_box = {};
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
    std::optional<wlr_scale_filter_mode> filter_mode = {};
};

/**
 * A simple wrapper for buffers which are used as render targets.
 * Note that a renderbuffer does not assume any ownership of the buffer.
 */
struct render_buffer_t
{
    render_buffer_t() = default;
    render_buffer_t(wlr_buffer *buffer, wf::dimensions_t size);

    /**
     * Get the backing buffer.
     */
    wlr_buffer *get_buffer() const
    {
        return buffer;
    }

    wf::dimensions_t get_size() const
    {
        return size;
    }

  private:
    friend struct auxilliary_buffer_t;

    // The wlr_buffer backing the framebuffer.
    wlr_buffer *buffer = NULL;

    wf::dimensions_t size = {0, 0};
};

/**
 * Hints for choosing a suitable underlying memory layout when allocating a buffer.
 */
struct buffer_allocation_hints_t
{
    bool needs_alpha = true;
};

/**
 * A class managing a buffer used for rendering purposes.
 * Typically, such buffers are used to composite several textures together, which are then composited onto
 * a final buffer.
 */
struct auxilliary_buffer_t
{
  public:
    auxilliary_buffer_t() = default;
    auxilliary_buffer_t(const auxilliary_buffer_t& other) = delete;
    auxilliary_buffer_t(auxilliary_buffer_t&& other);

    auxilliary_buffer_t& operator =(const auxilliary_buffer_t& other) = delete;
    auxilliary_buffer_t& operator =(auxilliary_buffer_t&& other);

    ~auxilliary_buffer_t();

    /**
     * Resize the framebuffer.
     * Note that this may change the underlying wlr_buffer/wlr_texture.
     *
     * @param width The desired width
     * @param height The desired height
     * @param scale The desired scale, so that the final size will be
     *              ceil(width * scale) x ceil(height * scale).
     * @return True if the buffer size changed, False if the size didn't change
     */
    bool allocate(wf::dimensions_t size, float scale = 1.0, buffer_allocation_hints_t hints = {});

    /**
     * Free the wlr_buffer/wlr_texture backing this framebuffer.
     */
    void free();

    /**
     * Get the currently allocated wlr_buffer.
     * Note that the wlr_buffer may be NULL if no buffer has been allocated yet.
     */
    wlr_buffer *get_buffer() const;

    /**
     * Get the currently allocated size.
     */
    wf::dimensions_t get_size() const;

    /**
     * Get the current buffer and size as a renderbuffer.
     */
    render_buffer_t get_renderbuffer() const;

    /**
     * Get the backing texture.
     * If no texture has been created for the buffer yet, a new texture will be created.
     */
    wlr_texture *get_texture();

  private:
    render_buffer_t buffer;

    // The wlr_texture creating from this framebuffer.
    wlr_texture *texture = NULL;
};

/**
 * A render target contains a render buffer and information on how to map
 * coordinates from the logical coordinate space (output-local coordinates, etc.)
 * to buffer coordinates.
 *
 * A render target may or not cover the full framebuffer.
 */
struct render_target_t : public render_buffer_t
{
    render_target_t() = default;
    explicit render_target_t(const render_buffer_t& buffer);
    explicit render_target_t(const auxilliary_buffer_t& buffer);

    // Describes the logical coordinates of the render area, in whatever
    // coordinate system the render target needs.
    wf::geometry_t geometry = {0, 0, 0, 0};

    wl_output_transform wl_transform = WL_OUTPUT_TRANSFORM_NORMAL;
    // The scale of a framebuffer is a hint at how bigger the actual framebuffer
    // is compared to the logical geometry. It is useful for plugins utilizing
    // auxiliary buffers in logical coordinates, so that they know they should
    // render with higher resolution and still get a crisp image on the screen.
    float scale = 1.0;

    // If set, the subbuffer indicates a subrectangle of the framebuffer which
    // is used instead of the full buffer. In that case, the logical @geometry
    // is mapped only to that subrectangle and not to the full framebuffer.
    // Note: (0,0) is top-left for subbuffer.
    std::optional<wf::geometry_t> subbuffer;

    /**
     * Get a render target which is the same as this, but whose geometry is
     * translated by @offset.
     */
    render_target_t translated(wf::point_t offset) const;

    /**
     * Get the geometry of the given box after projecting it onto the framebuffer.
     * In the values returned, (0,0) is top-left.
     *
     * The resulting geometry is affected by the framebuffer geometry, scale and
     * transform.
     */
    wlr_box framebuffer_box_from_geometry_box(wlr_box box) const;

    /**
     * Get the geometry of the given region after projecting it onto the framebuffer. This is the same as
     * iterating over the rects in the region and transforming them with framebuffer_box_from_geometry_box.
     */
    wf::region_t framebuffer_region_from_geometry_region(const wf::region_t& region) const;
};

namespace scene
{
class render_instance_t;
using render_instance_uptr = std::unique_ptr<render_instance_t>;
}

enum render_pass_flags
{
    /**
     * Do not emit render-pass-{begin, end} signals.
     */
    RPASS_EMIT_SIGNALS     = (1 << 0),
    /**
     * Do not clear the background areas.
     */
    RPASS_CLEAR_BACKGROUND = (1 << 1),
};

/**
 * A struct containing the information necessary to execute a render pass.
 */
struct render_pass_params_t
{
    /** The instances which are to be rendered in this render pass. */
    std::vector<scene::render_instance_uptr> *instances = NULL;

    /** The rendering target. */
    render_target_t target;

    /** The total damage accumulated from the instances since the last repaint. */
    region_t damage;

    /**
     * The background color visible below all instances, if
     * RPASS_CLEAR_BACKGROUND is specified.
     */
    color_t background_color;

    /**
     * The output the instances were rendered, used for sending presentation
     * feedback.
     */
    output_t *reference_output = nullptr;

    /**
     * The wlroots renderer to use for this pass.
     * In case that it is not set, wf::get_core().renderer will be used.
     */
    wlr_renderer *renderer = nullptr;

    /**
     * Additional options for the wlroots buffer pass.
     */
    wlr_buffer_pass_options *pass_opts = nullptr;

    /**
     * Flags for this render pass, see @render_pass_flags.
     */
    uint32_t flags = 0;
};

/**
 * A render pass is used to generate and execute a set of drawing commands to the same render target.
 */
class render_pass_t
{
    render_pass_params_t params;
    wlr_render_pass *pass = NULL;

  public:
    render_pass_t(const render_pass_params_t& params);

    // Cannot copy a render pass: we would need to duplicate all the render commands, which does not make
    // sense.
    render_pass_t(const render_pass_t& other) = delete;
    render_pass_t& operator =(const render_pass_t& other) = delete;

    render_pass_t(render_pass_t&& other);
    render_pass_t& operator =(render_pass_t&& other);
    ~render_pass_t();

    /**
     * Run a new render pass from start to finish.
     * This includes generate instructions for the render pass and executing them.
     *
     * The render pass goes as described below:
     *
     * 1. Optionally, emit render-pass-begin.
     * 2. Render instructions are generated from the given instances. During this phase, the instances may
     *    start and execute sub-passes.
     * 3. The wlroots render pass begins.
     * 4. Optionally, clear visible background areas with @background_color.
     * 5. Render instructions are executed back-to-forth.
     * 6. Optionally, emit render-pass-end.
     * 7. The wlroots render pass is submitted.
     *
     * By specifying @flags, steps 1, 4, and 6 can be enabled and disabled.
     *
     * @return The full damage which was rendered on the render target. It may be more (or
     *  less) than @params.damage because plugins are allowed to modify the
     *  damage in render-pass-begin.
     */
    static wf::region_t run(const wf::render_pass_params_t& params);

    /**
     * Same as @run, but does not submit the wlroots render pass (i.e step 7 is omitted).
     */
    wf::region_t run_partial();

    /**
     * The current wlroots render pass.
     * Note that one Wayfire pass may result in multiple wlroots render passes, if the render commands are
     * interspersed with custom rendering code in plugins, so this pointer may change over the duration of
     * the Wayfire render pass.
     */
    wlr_render_pass *get_wlr_pass();

    /**
     * Clear the given region (relative to the render target's geometry) with the given color.
     */
    void clear(const wf::region_t& region, const wf::color_t& color);

    /**
     * Add a texture rendering operation to the pass.
     */
    void add_texture(const wf::texture_t& texture,
        const wf::render_target_t& adjusted_target,
        const wf::geometry_t& geometry,
        const wf::region_t& damage,
        float alpha = 1.0);

    /**
     * Add a colored rectangle to the pass.
     */
    void add_rect(const wf::color_t& color,
        const wf::render_target_t& adjusted_target,
        const wf::geometry_t& geometry,
        const wf::region_t& damage);

    /**
     * Get the wlr_renderer used in this pass.
     */
    wlr_renderer *get_wlr_renderer() const;

    /**
     * Get the render target.
     */
    wf::render_target_t get_target() const;

    /**
     * Submit the wlroots render pass.
     * Should only be used after run_partial().
     */
    bool submit();

    /**
     * A helper function for plugins which support custom OpenGL ES rendering.
     *
     * The callback is executed when running with the wlroots GLES renderer and is simply skipped otherwise.
     * It is guaranteed that if it is executed, then the pass' target buffer will be bound as the draw
     * framebuffer and its full size set as the viewport. In addition, the blending mode (1, 1-src_alpha)
     * will be enabled.
     *
     * Plugins need to reset any GL state that they change after this callback except the bound draw
     * framebuffer, the viewport and the currently bound program.
     *
     * The subpass functionality is intended to be used for custom rendering and could be used to support
     * both Vulkan and GLES rendering with minimum effort by running one GLES and one Vulkan subpass.
     * Depending on the active renderer, one of them will be skipped.
     */
    template<class F>
    bool custom_gles_subpass(F&& fn)
    {
        if (prepare_gles_subpass())
        {
            fn();
            finish_gles_subpass();
            return true;
        }

        return false;
    }

    template<class F>
    bool custom_gles_subpass(const wf::render_target_t& target, F&& fn)
    {
        if (prepare_gles_subpass())
        {
            fn();
            finish_gles_subpass();
            return true;
        }

        return false;
    }

  private:
    bool prepare_gles_subpass();
    bool prepare_gles_subpass(const wf::render_target_t& target);
    void finish_gles_subpass();
};


/**
 * Signal that a render pass starts.
 * emitted on: core.
 */
struct render_pass_begin_signal
{
    render_pass_begin_signal(wf::render_pass_t& pass, wf::region_t& damage) :
        damage(damage), pass(pass)
    {}

    /**
     * The initial damage for this render pass.
     * Plugins may expand it further.
     */
    wf::region_t& damage;

    /**
     * The render pass that is starting.
     */
    wf::render_pass_t& pass;
};

/**
 * Signal that is emitted once a render pass ends.
 * emitted on: core.
 */
struct render_pass_end_signal
{
    render_pass_end_signal(wf::render_pass_t& pass) :
        pass(pass)
    {}

    wf::render_pass_t& pass;
};
}
