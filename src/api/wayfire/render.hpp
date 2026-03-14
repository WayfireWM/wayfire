#pragma once

#ifdef WF_USE_CONFIG_H
    #include <config.h>
#else
    #include <wayfire/config.h>
#endif

#include "wayfire/signal-provider.hpp"
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
struct auxilliary_buffer_t;
namespace vk
{
class command_buffer_t;
}

/**
 * A struct which describes the color space of a given texture.
 */
struct color_transform_t
{
    wlr_color_transfer_function transfer_function = WLR_COLOR_TRANSFER_FUNCTION_SRGB;
    wlr_color_named_primaries primaries = WLR_COLOR_NAMED_PRIMARIES_SRGB;
    wlr_color_encoding color_encoding   = WLR_COLOR_ENCODING_NONE;
    wlr_color_range color_range = WLR_COLOR_RANGE_NONE;

    bool operator ==(const color_transform_t& other) const;
    bool operator !=(const color_transform_t& other) const;
};

/**
 * A wrapper around wlr_texture which ensures that the texture is kept alive as long as the wrapper object
 * is alive.
 */
class texture_t : public wf::signal::provider_t
{
  public:
    /**
     * Emitted on destruction of the texture_t object, i.e when the texture is about to be destroyed.
     */
    struct destroy_signal
    {
        texture_t *self;
    };

    // Getters and setters for source_box
    std::optional<wlr_fbox> get_source_box() const;
    void set_source_box(const std::optional<wlr_fbox>& box);

    // Getters and setters for transform
    wl_output_transform get_transform() const;
    void set_transform(wl_output_transform t);

    // Getters and setters for filter_mode
    std::optional<wlr_scale_filter_mode> get_filter_mode() const;
    void set_filter_mode(const std::optional<wlr_scale_filter_mode>& mode);

    // Getters and setters for color_transform
    color_transform_t get_color_transform() const;
    void set_color_transform(const color_transform_t& ct);

    /**
     * Manage an existing wlr_texture created from a wlr_buffer.
     * We keep the texture alive by keeping the associated buffer alive.
     */
    static std::shared_ptr<texture_t> from_buffer(wlr_buffer *buffer, wlr_texture *texture);

    /**
     * Manage an existing wlr_texture created without a wlr_buffer.
     * In this case, the caller must have ownership of the texture.
     * The ownership is then passed to the returned texture_t object.
     */
    static std::shared_ptr<texture_t> from_texture(wlr_texture *texture);

    /**
     * Create a texture from an auxilliary buffer.
     * The auxilliary buffer must have a valid buffer and texture allocated.
     */
    static std::shared_ptr<texture_t> from_aux(auxilliary_buffer_t& buffer);

    /**
     * Get the width of the texture in pixels.
     */
    int32_t get_width() const;

    /**
     * Get the height of the texture in pixels.
     */
    int32_t get_height() const;

    ~texture_t();

    /**
     * Get the underlying wlr_texture pointer.
     */
    wlr_texture *get_wlr_texture() const;

  private:
    texture_t();

    wlr_texture *texture = NULL;
    wlr_buffer *buffer   = NULL; // If NULL, we own @texture.

    std::optional<wlr_fbox> source_box = {};
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
    std::optional<wlr_scale_filter_mode> filter_mode = {};
    color_transform_t color_transform;
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

    /**
     * Copy a part of another buffer onto this buffer.
     * Note that this operation may involve the creation and deletion of textures, which can reset the
     * GL state.
     *
     * @param source The source buffer containing the pixel data to be copied from.
     * @param src_box The subrectangle of the source buffer to be copied from.
     * @param dst_box The subrectangle of the destination buffer to be copied to.
     * @param filter_mode The filter mode to use.
     */
    void blit(wf::auxilliary_buffer_t& source, wlr_fbox src_box, wf::geometry_t dst_box,
        wlr_scale_filter_mode filter_mode = WLR_SCALE_FILTER_BILINEAR) const;

    /**
     * Copy a part of another buffer onto this buffer.
     * Note that this operation may involve the creation and deletion of textures, which can reset the
     * GL state.
     *
     * @param source The source buffer containing the pixel data to be copied from.
     * @param src_box The subrectangle of the source buffer to be copied from.
     * @param dst_box The subrectangle of the destination buffer to be copied to.
     * @param filter_mode The filter mode to use.
     */
    void blit(const wf::render_buffer_t& source, wlr_fbox src_box, wf::geometry_t dst_box,
        wlr_scale_filter_mode filter_mode = WLR_SCALE_FILTER_BILINEAR) const;

  private:
    friend struct auxilliary_buffer_t;

    // The wlr_buffer backing the framebuffer.
    wlr_buffer *buffer = NULL;

    wf::dimensions_t size = {0, 0};

    // Helper for copy operations
    void do_blit(wlr_texture *src_wlr_tex, wlr_fbox src_box, wf::geometry_t dst_box,
        wlr_scale_filter_mode filter_mode) const;
};

/**
 * Hints for choosing a suitable underlying memory layout when allocating a buffer.
 */
struct buffer_allocation_hints_t
{
    bool needs_alpha = true;
};

/**
 * Result of an allocate() call for an auxilliary buffer.
 */
enum class buffer_reallocation_result_t
{
    /** Buffer does not need reallocation (i.e buffer already had a good size) */
    SAME,
    /** Buffer was successfully reallocated to the new size. */
    REALLOCATED,
    /** Buffer reallocation failed. */
    FAILED,
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
     *
     * @return The result of the reallocation operation.
     */
    buffer_reallocation_result_t allocate(wf::dimensions_t size, float scale = 1.0,
        buffer_allocation_hints_t hints = {});

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
     * Get the geometry of the given fbox after projecting it onto the framebuffer.
     * In the values returned, (0,0) is top-left.
     *
     * The resulting geometry is affected by the framebuffer geometry, scale and
     * transform.
     */
    wlr_fbox framebuffer_box_from_geometry_box(wlr_fbox box) const;

    /**
     * Get the geometry of the given region after projecting it onto the framebuffer. This is the same as
     * iterating over the rects in the region and transforming them with framebuffer_box_from_geometry_box.
     */
    wf::region_t framebuffer_region_from_geometry_region(const wf::region_t& region) const;

    /**
     * Get the geometry of the given framebuffer box after projecting it back to the logical coordinate space.
     *
     * The resulting geometry is affected by the framebuffer geometry, scale and
     * transform.
     */
    wlr_box geometry_box_from_framebuffer_box(wlr_box fb_box) const;

    /**
     * Get the geometry of the given framebuffer fbox after projecting it back to the logical coordinate
     * space.
     *
     * The resulting geometry is affected by the framebuffer geometry, scale and
     * transform.
     */
    wlr_fbox geometry_fbox_from_framebuffer_box(wlr_fbox fb_box) const;

    /**
     * Get the geometry of the given framebuffer region after projecting it back to the logical coordinate
     * space.
     * This is the same as iterating over the rects in the region and transforming them with
     * geometry_box_from_framebuffer_box.
     */
    wf::region_t geometry_region_from_framebuffer_region(const wf::region_t& region) const;
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

class vulkan_render_state_t;

/**
 * A render pass is used to generate and execute a set of drawing commands to the same render target.
 */
class render_pass_t
{
    render_pass_params_t params;
    wlr_render_pass *_pass = NULL;

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
     * Create, run and submit a render pass in one go.
     *
     * Equivalent to:
     *
     * ```
     * render_pass_t pass{params};
     * pass.run_partial();
     * pass.submit();
     * ```
     *
     * @return The full damage which was rendered on the render target, as described in @run_partial().
     */
    static wf::region_t run(const wf::render_pass_params_t& params);

    /**
     * Execute the main part of a render pass.
     * This involves the following steps:
     *
     * 1. Optionally, emit render-pass-begin.
     * 2. Render instructions are generated from the given instances. During this phase, the instances may
     *    start and execute sub-passes.
     * 3. The wlroots render pass begins.
     * 4. Optionally, clear visible background areas with @background_color.
     * 5. Render instructions are executed back-to-front, i.e starting with the last instruction in the list.
     * 6. Optionally, emit render-pass-end.
     *
     * By specifying @render_pass_params_t::flags, steps 1, 4, and 6 can be enabled and disabled.
     * After @run_partial() returns, additional render operations may be added to the pass, and finally
     * the pass needs to be submitted with @submit() to ensure that all operations are executed.
     *
     * @return The full damage which was rendered on the render target. It may be more (or
     *  less) than @params.damage because plugins are allowed to modify the damage in render-pass-begin.
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
    void add_texture(const std::shared_ptr<wf::texture_t>& texture,
        const wf::render_target_t& adjusted_target,
        const wf::geometry_t& geometry,
        const wf::region_t& damage,
        float alpha = 1.0);

    /**
     * Add a texture rendering operation to the pass using wlr_fbox for geometry.
     */
    void add_texture(const std::shared_ptr<wf::texture_t>& texture,
        const wf::render_target_t& adjusted_target,
        const wlr_fbox& geometry,
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
     * Add a colored rectangle to the pass using wlr_fbox for geometry.
     */
    void add_rect(const wf::color_t& color,
        const wf::render_target_t& adjusted_target,
        const wlr_fbox& geometry,
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

#if WF_HAS_VULKANFX
    template<class F>
    bool custom_vulkan_subpass(F&& fn)
    {
        if (auto state = prepare_vulkan_subpass())
        {
            fn(*state, *active_command_buffer);
            end_vulkan_subpass();
            return true;
        }

        return false;
    }

#endif

  private:

#if WF_HAS_VULKANFX
    vk::command_buffer_t *active_command_buffer = nullptr;
    vulkan_render_state_t *prepare_vulkan_subpass();
    void end_vulkan_subpass();
#endif

    bool prepare_gles_subpass();
    bool prepare_gles_subpass(const wf::render_target_t& target);
    void finish_gles_subpass();

    bool needs_restart = false;
    wlr_render_pass *_get_pass();
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
