#pragma once

#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include <string>
#include <wayfire/config/types.hpp>
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <wayfire/dassert.hpp>
#include <wayfire/render.hpp>

// TODO: do we need some kind of dependency here?
#include <drm_fourcc.h>

namespace wf
{
/**
 * Very basic wrapper around wlr_texture.
 * It destroys the texture automatically.
 */
struct owned_texture_t
{
    owned_texture_t(const owned_texture_t& other) = delete;
    owned_texture_t& operator =(const owned_texture_t& other) = delete;

    owned_texture_t(owned_texture_t&& other) : owned_texture_t()
    {
        std::swap(tex, other.tex);
        std::swap(size, other.size);
    }

    owned_texture_t& operator =(owned_texture_t&& other)
    {
        if (&other == this)
        {
            return *this;
        }

        if (tex)
        {
            wlr_texture_destroy(tex);
        }

        tex = other.tex;
        other.tex = NULL;

        size = other.size;
        other.size = {0, 0};
        return *this;
    }

    ~owned_texture_t()
    {
        if (tex)
        {
            wlr_texture_destroy(tex);
        }
    }

    wf::texture_t get_texture() const
    {
        return wf::texture_t{tex};
    }

    wf::dimensions_t get_size() const
    {
        return size;
    }

    // Empty texture.
    owned_texture_t()
    {}

    // Assumes ownership of the texture!
    owned_texture_t(wlr_texture *new_tex)
    {
        tex = new_tex;
    }

    owned_texture_t(cairo_surface_t *surface)
    {
        int width  = cairo_image_surface_get_width(surface);
        int height = cairo_image_surface_get_height(surface);
        int stride = cairo_image_surface_get_stride(surface);
        cairo_format_t fmt = cairo_image_surface_get_format(surface);
        uint32_t drm_fmt   = 0;

        if ((width <= 0) || (height <= 0))
        {
            // empty texture
            return;
        }

        switch (fmt)
        {
          case CAIRO_FORMAT_ARGB32:
            drm_fmt = WL_SHM_FORMAT_ABGR8888;
            break;

          default:
            wf::dassert(false, "Unsupported cairo format: " + std::to_string(fmt) + "!");
        }

        this->tex = wlr_texture_from_pixels(wf::get_core().renderer, drm_fmt, stride, width, height,
            cairo_image_surface_get_data(surface));
        this->size = {width, height};
    }

  private:
    wlr_texture *tex = NULL;
    wf::dimensions_t size = {0, 0};
};

/**
 * Simple wrapper around rendering text with Cairo. This object can be
 * kept around to avoid reallocation of the cairo surface and OpenGL
 * texture on repeated renders.
 */
struct cairo_text_t
{
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
            if (par.exact_size || (w > surface_size.width) || (h > surface_size.height))
            {
                cairo_create_surface({w, h});
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
        this->tex = owned_texture_t{surface};
        return ret;
    }

    cairo_text_t() = default;
    ~cairo_text_t()
    {
        cairo_free();
    }

    cairo_text_t(const cairo_text_t &) = delete;
    cairo_text_t& operator =(const cairo_text_t&) = delete;

    cairo_text_t(cairo_text_t && o) noexcept : cr(o.cr), surface(o.surface),
        surface_size(o.surface_size), tex(std::move(o.tex))
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
        dummy.cairo_create_surface({1, 1});

        cairo_font_extents_t font_extents;
        /* TODO: font properties could be made parameters! */
        cairo_select_font_face(dummy.cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(dummy.cr, font_size);
        cairo_font_extents(dummy.cr, &font_extents);

        double ypad    = bg_rect ? 0.2 * (font_extents.ascent + font_extents.descent) : 0.0;
        unsigned int h = (unsigned int)std::ceil(font_extents.ascent + font_extents.descent + 2 * ypad);
        return h;
    }

    wf::dimensions_t get_size() const
    {
        return surface_size;
    }

    wf::texture_t get_texture() const
    {
        return this->tex.get_texture();
    }

  protected:
    /* cairo context and surface for the text */
    cairo_t *cr = nullptr;
    cairo_surface_t *surface = nullptr;
    /* current width and height of the above surface */
    wf::dimensions_t surface_size = {0, 0};


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

    void cairo_create_surface(wf::dimensions_t size = {400, 100})
    {
        cairo_free();
        this->surface_size = size;
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surface_size.width, surface_size.height);
        cr = cairo_create(surface);
    }

    owned_texture_t tex;
};
}
