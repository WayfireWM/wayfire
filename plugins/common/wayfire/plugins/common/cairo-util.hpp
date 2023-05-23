#pragma once

#include "wayfire/geometry.hpp"
#include <string>
#include <wayfire/plugins/common/simple-texture.hpp>
#include <wayfire/config/types.hpp>
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

namespace wf
{
struct simple_texture_t;
}

/**
 * Upload the data from the cairo surface to the OpenGL texture.
 *
 * @param surface The source cairo surface.
 * @param buffer  The buffer to upload data to.
 */
static void cairo_surface_upload_to_texture(
    cairo_surface_t *surface, wf::simple_texture_t& buffer)
{
    buffer.width  = cairo_image_surface_get_width(surface);
    buffer.height = cairo_image_surface_get_height(surface);
    if (buffer.tex == (GLuint) - 1)
    {
        GL_CALL(glGenTextures(1, &buffer.tex));
    }

    auto src = cairo_image_surface_get_data(surface);

    GL_CALL(glBindTexture(GL_TEXTURE_2D, buffer.tex));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED));
    GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
        buffer.width, buffer.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, src));
}

namespace wf
{
/**
 * Simple wrapper around rendering text with Cairo. This object can be
 * kept around to avoid reallocation of the cairo surface and OpenGL
 * texture on repeated renders.
 */
struct cairo_text_t
{
    wf::simple_texture_t tex;

    /* parameters used for rendering */
    struct params
    {
        /* font size */
        int font_size = 12;
        /* color for background rectangle (only used if bg_rect == true) */
        wf::color_t bg_color;
        /* text color */
        wf::color_t text_color;
        /* scale everything by this amount */
        float output_scale = 1.f;
        /* crop result to this size (if nonzero);
         * note that this is multiplied by output_scale */
        wf::dimensions_t max_size{0, 0};
        /* draw a rectangle in the background with bg_color */
        bool bg_rect = true;
        /* round the corners of the background rectangle */
        bool rounded_rect = true;
        /* if true, the resulting surface will be cropped to the
         * minimum size necessary to fit the text; otherwise, the
         * resulting surface might be bigger than necessary and the
         * text is centered in it */
        bool exact_size = false;

        params()
        {}
        params(int font_size_, const wf::color_t& bg_color_,
            const wf::color_t& text_color_, float output_scale_ = 1.f,
            const wf::dimensions_t& max_size_ = {0, 0},
            bool bg_rect_ = true, bool exact_size_ = false) :
            font_size(font_size_), bg_color(bg_color_),
            text_color(text_color_), output_scale(output_scale_),
            max_size(max_size_), bg_rect(bg_rect_),
            exact_size(exact_size_)
        {}
    };

    /**
     * Render the given text in the texture tex.
     *
     * @param text         text to render
     * @param par          parameters for rendering
     *
     * @return The size needed to render in scaled coordinates. If this is larger
     *   than the size of tex, it means the result was cropped (due to the constraint
     *   given in par.max_size). If it is smaller, than the result is centered along
     *   that dimension.
     */
    wf::dimensions_t render_text(const std::string& text, const params& par)
    {
        if (!cr)
        {
            /* create with default size */
            cairo_create_surface();
        }

        PangoFontDescription *font_desc;
        PangoLayout *layout;
        PangoRectangle extents;
        /* TODO: font properties could be made parameters! */
        font_desc = pango_font_description_from_string("sans-serif bold");
        pango_font_description_set_absolute_size(font_desc,
            par.font_size * par.output_scale * PANGO_SCALE);
        layout = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(layout, font_desc);
        pango_layout_set_text(layout, text.c_str(), text.size());
        pango_layout_get_extents(layout, NULL, &extents);

        double xpad = par.bg_rect ? 10.0 * par.output_scale : 0.0;
        double ypad = par.bg_rect ?
            0.2 * ((float)extents.height / PANGO_SCALE) : 0.0;
        int w = (int)((float)extents.width / PANGO_SCALE + 2 * xpad);
        int h = (int)((float)extents.height / PANGO_SCALE + 2 * ypad);
        wf::dimensions_t ret = {w, h};
        if (par.max_size.width && (w > par.max_size.width * par.output_scale))
        {
            w = (int)std::floor(par.max_size.width * par.output_scale);
        }

        if (par.max_size.height && (h > par.max_size.height * par.output_scale))
        {
            h = (int)std::floor(par.max_size.height * par.output_scale);
        }

        if ((w != surface_size.width) || (h != surface_size.height))
        {
            if (par.exact_size || (w > surface_size.width) ||
                (h > surface_size.height))
            {
                surface_size.width  = w;
                surface_size.height = h;
                cairo_create_surface();
            }
        }

        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);

        int x = (surface_size.width - w) / 2;
        int y = (surface_size.height - h) / 2;

        if (par.bg_rect)
        {
            int min_r = (int)(20 * par.output_scale);
            int r     = par.rounded_rect ? (h > min_r ? min_r : (h - 2) / 2) : 0;

            cairo_move_to(cr, x + r, y);
            cairo_line_to(cr, x + w - r, y);
            if (par.rounded_rect)
            {
                cairo_curve_to(cr, x + w, y, x + w, y, x + w, y + r);
            }

            cairo_line_to(cr, x + w, y + h - r);
            if (par.rounded_rect)
            {
                cairo_curve_to(cr, x + w, y + h, x + w, y + h, x + w - r, y + h);
            }

            cairo_line_to(cr, x + r, y + h);
            if (par.rounded_rect)
            {
                cairo_curve_to(cr, x, y + h, x, y + h, x, y + h - r);
            }

            cairo_line_to(cr, x, y + r);
            if (par.rounded_rect)
            {
                cairo_curve_to(cr, x, y, x, y, x + r, y);
            }

            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_rgba(cr, par.bg_color.r, par.bg_color.g,
                par.bg_color.b, par.bg_color.a);
            cairo_fill(cr);
        }

        x += xpad;
        y += ypad;

        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_move_to(cr, x - (float)extents.x / PANGO_SCALE, y);
        cairo_set_source_rgba(cr, par.text_color.r, par.text_color.g,
            par.text_color.b, par.text_color.a);

        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(font_desc);
        g_object_unref(layout);

        cairo_surface_flush(surface);
        OpenGL::render_begin();
        cairo_surface_upload_to_texture(surface, tex);
        OpenGL::render_end();

        return ret;
    }

    /**
     * Standalone function version to render text to an OpenGL texture
     */
    static wf::dimensions_t cairo_render_text_to_texture(const std::string& text,
        const wf::cairo_text_t::params& par, wf::simple_texture_t& tex)
    {
        wf::cairo_text_t ct;
        /* note: we "borrow" the texture from what was supplied (if any) */
        ct.tex.tex = tex.tex;
        auto ret = ct.render_text(text, par);
        if (tex.tex == (GLuint) - 1)
        {
            tex.tex = ct.tex.tex;
        }

        tex.width  = ct.tex.width;
        tex.height = ct.tex.height;
        ct.tex.tex = -1;
        return ret;
    }

    cairo_text_t() = default;

    ~cairo_text_t()
    {
        cairo_free();
    }

    cairo_text_t(const cairo_text_t &) = delete;
    cairo_text_t& operator =(const cairo_text_t&) = delete;

    cairo_text_t(cairo_text_t && o) noexcept : tex(std::move(o.tex)), cr(o.cr),
        surface(o.surface), surface_size(o.surface_size)
    {
        o.cr = nullptr;
        o.surface = nullptr;
    }

    cairo_text_t& operator =(cairo_text_t&& o) noexcept
    {
        if (&o == this)
        {
            return *this;
        }

        cairo_free();

        tex = std::move(o.tex);
        cr  = o.cr;
        surface = o.surface;
        surface_size = o.surface_size;

        o.cr = nullptr;
        o.surface = nullptr;

        return *this;
    }

    /**
     * Calculate the height of text rendered with a given font size.
     *
     * @param font_size  Desired font size.
     * @param bg_rect    Whether a background rectangle should be taken into account.
     *
     * @returns Required height of the surface.
     */
    static unsigned int measure_height(int font_size, bool bg_rect = true)
    {
        cairo_text_t dummy;
        dummy.surface_size.width  = 1;
        dummy.surface_size.height = 1;
        dummy.cairo_create_surface();

        cairo_font_extents_t font_extents;
        /* TODO: font properties could be made parameters! */
        cairo_select_font_face(dummy.cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
            CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(dummy.cr, font_size);
        cairo_font_extents(dummy.cr, &font_extents);

        double ypad = bg_rect ? 0.2 * (font_extents.ascent +
            font_extents.descent) : 0.0;
        unsigned int h = (unsigned int)std::ceil(font_extents.ascent +
            font_extents.descent + 2 * ypad);
        return h;
    }

    wf::dimensions_t get_size() const
    {
        return surface_size;
    }

  protected:
    /* cairo context and surface for the text */
    cairo_t *cr = nullptr;
    cairo_surface_t *surface = nullptr;
    /* current width and height of the above surface */
    wf::dimensions_t surface_size = {400, 100};


    void cairo_free()
    {
        if (cr)
        {
            cairo_destroy(cr);
        }

        if (surface)
        {
            cairo_surface_destroy(surface);
        }

        cr = nullptr;
        surface = nullptr;
    }

    void cairo_create_surface()
    {
        cairo_free();
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surface_size.width,
            surface_size.height);
        cr = cairo_create(surface);
    }
};
}
