#pragma once

#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/region.hpp>
#include <optional>

#define GLM_FORCE_RADIANS
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace wf
{
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
    bool allocate(wf::dimensions_t size, float scale = 1.0);

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
}
