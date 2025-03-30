#include <wayfire/render.hpp>
#include "core/core-impl.hpp"
#include "wayfire/dassert.hpp"
#include <drm_fourcc.h>

wf::render_buffer_t::render_buffer_t(wlr_buffer *buffer, wf::dimensions_t size)
{
    this->buffer = buffer;
    this->size   = size;
}

wf::auxilliary_buffer_t::auxilliary_buffer_t(auxilliary_buffer_t&& other)
{
    *this = std::move(other);
}

wf::auxilliary_buffer_t& wf::auxilliary_buffer_t::operator =(auxilliary_buffer_t&& other)
{
    if (&other == this)
    {
        return *this;
    }

    this->buffer = other.buffer;
    other.buffer.buffer = NULL;
    return *this;
}

wf::auxilliary_buffer_t::~auxilliary_buffer_t()
{
    free();
}

bool wf::auxilliary_buffer_t::allocate(wf::dimensions_t size, float scale)
{
    size.width  = std::ceil(size.width * scale);
    size.height = std::ceil(size.height * scale);

    if (buffer.get_size() == size)
    {
        return false;
    }

    free();

    auto renderer = wf::get_core().renderer;
    auto supported_render_formats =
        wlr_renderer_get_texture_formats(wf::get_core().renderer, renderer->render_buffer_caps);
    auto format = wlr_drm_format_set_get(supported_render_formats, DRM_FORMAT_ABGR8888);
    if (!format)
    {
        LOGE("Failed to find supported render format!");
        return false;
    }

    buffer.buffer = wlr_allocator_create_buffer(wf::get_core_impl().allocator, size.width,
        size.height, format);

    if (!buffer.buffer)
    {
        LOGE("Failed to allocate auxilliary buffer!");
        return false;
    }

    buffer.size = size;
    return true;
}

void wf::auxilliary_buffer_t::free()
{
    if (texture)
    {
        wlr_texture_destroy(texture);
    }

    texture = NULL;

    if (buffer.get_buffer())
    {
        wlr_buffer_drop(buffer.get_buffer());
    }

    buffer.buffer = NULL;
}

wlr_buffer*wf::auxilliary_buffer_t::get_buffer() const
{
    return buffer.get_buffer();
}

wf::dimensions_t wf::auxilliary_buffer_t::get_size() const
{
    return buffer.get_size();
}

wlr_texture*wf::auxilliary_buffer_t::get_texture()
{
    wf::dassert(buffer.get_buffer(), "No buffer allocated yet!");
    if (!texture)
    {
        texture = wlr_texture_from_buffer(wf::get_core().renderer, buffer.get_buffer());
    }

    return texture;
}

wf::render_buffer_t wf::auxilliary_buffer_t::get_renderbuffer() const
{
    return buffer;
}

wf::render_target_t::render_target_t(const render_buffer_t& buffer) : render_buffer_t(buffer)
{}
wf::render_target_t::render_target_t(const auxilliary_buffer_t& buffer) : render_buffer_t(
        buffer.get_buffer(), buffer.get_size())
{}

wlr_box wf::render_target_t::framebuffer_box_from_geometry_box(wlr_box box) const
{
    /* Step 1: Make relative to the framebuffer */
    box.x -= this->geometry.x;
    box.y -= this->geometry.y;

    /* Step 2: Apply scale to box */
    wlr_box scaled = box * scale;

    /* Step 3: rotate */
    wf::dimensions_t size = get_size();
    if (wl_transform & 1)
    {
        std::swap(size.width, size.height);
    }

    wlr_box result;
    wl_output_transform transform =
        wlr_output_transform_invert((wl_output_transform)wl_transform);

    wlr_box_transform(&result, &scaled, transform, size.width, size.height);

    if (subbuffer)
    {
        result = scale_box({0, 0, get_size().width, get_size().height},
            subbuffer.value(), result);
    }

    return result;
}

wf::region_t wf::render_target_t::framebuffer_region_from_geometry_region(const wf::region_t& region) const
{
    wf::region_t result;
    for (const auto& rect : region)
    {
        result |= framebuffer_box_from_geometry_box(wlr_box_from_pixman_box(rect));
    }

    return result;
}
