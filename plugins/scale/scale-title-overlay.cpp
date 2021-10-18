#include "scale-title-overlay.hpp"

#include <wayfire/opengl.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/plugins/common/simple-texture.hpp>

/**
 * Get the topmost parent of a view.
 */
static wayfire_view find_toplevel_parent(wayfire_view view)
{
    while (view->parent)
    {
        view = view->parent;
    }

    return view;
}

/**
 * Class storing an overlay with a view's title, only stored for parent views.
 */
struct view_title_texture_t : public wf::custom_data_t
{
    wayfire_view view;
    wf::cairo_text_t overlay;
    wf::cairo_text_t::params par;
    bool overflow = false;
    wayfire_view dialog; /* the texture should be rendered on top of this dialog */

    /**
     * Render the overlay text in our texture, cropping it to the size by
     * the given box.
     */
    void update_overlay_texture(wf::dimensions_t dim)
    {
        par.max_size = dim;
        update_overlay_texture();
    }

    void update_overlay_texture()
    {
        auto res = overlay.render_text(view->get_title(), par);
        overflow = res.width > overlay.tex.width;
    }

    wf::signal_connection_t view_changed = [this] (auto)
    {
        if (overlay.tex.tex != (GLuint) - 1)
        {
            update_overlay_texture();
        }
    };

    view_title_texture_t(wayfire_view v, int font_size, const wf::color_t& bg_color,
        const wf::color_t& text_color, float output_scale) : view(v)
    {
        par.font_size    = font_size;
        par.bg_color     = bg_color;
        par.text_color   = text_color;
        par.exact_size   = true;
        par.output_scale = output_scale;

        view->connect_signal("title-changed", &view_changed);
    }
};

/**
 * Class with the overlay hooks, added to scale's transformer.
 */
class view_title_overlay_t : public wf::scale_transformer_t::overlay_t
{
  public:
    enum class position
    {
        TOP,
        CENTER,
        BOTTOM,
    };

  protected:
    /* the transformer we are attached to */
    wf::scale_transformer_t& tr;
    /* save the transformed view, since we need it in the destructor */
    wayfire_view view;
    /* the position on the screen we currently render to */
    wf::geometry_t geometry;
    scale_show_title_t& parent;
    unsigned int text_height; /* set in the constructor, should not change */
    position pos = position::CENTER;
    /* Whether we are currently rendering the overlay by this transformer.
     * Set in the pre-render hook and used in the render function. */
    bool overlay_shown = false;

    /**
     * Get the transformed WM geometry of the view transformed by the given
     * transformer, including the current transform, but not any padding.
     */
    wlr_box get_transformed_wm_geometry(wf::scale_transformer_t& tr)
    {
        wlr_box box = tr.get_transformed_view()->get_wm_geometry();
        return tr.trasform_box_without_padding(box);
    }

    wlr_box get_transformed_wm_geometry()
    {
        return get_transformed_wm_geometry(tr);
    }

    /**
     * Get the transformed WM geometry of the given view,
     * including the current transform, but not any padding.
     */
    wlr_box get_transformed_wm_geometry(wayfire_view view)
    {
        auto tmp =
            view->get_transformer(wf::scale_transformer_t::transformer_name());
        if (!tmp)
        {
            /* This might happen if view is a newly created dialog that does not
             * yet have a transformed added to it. In this case, return a zero
             * size box that will never overlap with the overlay. */
            return {0, 0, 0, 0};
        }

        /* TODO: can we use static_cast in this case? */
        auto tr = dynamic_cast<wf::scale_transformer_t*>(tmp.get());
        assert(tr);

        return get_transformed_wm_geometry(*tr);
    }

    /**
     * Gets the overlay texture stored with the given view.
     */
    view_title_texture_t& get_overlay_texture(wayfire_view view)
    {
        auto data = view->get_data<view_title_texture_t>();
        if (!data)
        {
            auto new_data = new view_title_texture_t(view, parent.title_font_size,
                parent.bg_color, parent.text_color, parent.output->handle->scale);
            view->store_data<view_title_texture_t>(std::unique_ptr<view_title_texture_t>(
                new_data));
            return *new_data;
        }

        return *data.get();
    }

    /**
     * Get the bounding box of the topmost parent of this view without and
     * padding added by scale's transformer.
     */
    wlr_box get_parent_box()
    {
        auto view = tr.get_transformed_view();
        auto box  = get_transformed_wm_geometry(find_toplevel_parent(view));
        if ((box.width == 0) || (box.height == 0))
        {
            /* This is the case if the parent does not have a transformer. This
             * should normally not happen, but might be the case if this view is
             * assigned as the child of a newly created view that does not yet
             * have a transformer.
             * TODO: check if this case is actually possible! */
            return get_transformed_wm_geometry();
        }

        return box;
    }

    /**
     * Check if this view should display an overlay.
     */
    bool should_have_overlay(view_title_texture_t& title)
    {
        if (this->parent.show_view_title_overlay ==
            scale_show_title_t::title_overlay_t::NEVER)
        {
            return false;
        }

        auto parent = find_toplevel_parent(view);

        if ((this->parent.show_view_title_overlay ==
             scale_show_title_t::title_overlay_t::MOUSE) &&
            (this->parent.last_title_overlay != parent))
        {
            return false;
        }

        if (view == parent)
        {
            /* Check if the overlay overlaps with any dialogs. */

            /* Update maximum possible extents of the overlay */
            auto max_geom = get_transformed_wm_geometry();
            switch (pos)
            {
              case position::CENTER:
                max_geom.y += (max_geom.height - text_height) / 2;
                break;

              case position::TOP:
                max_geom.y -= (text_height + 1);
                break;

              case position::BOTTOM:
                max_geom.y += max_geom.height;
                break;
            }

            max_geom.height = text_height + 1;

            title.dialog = view;
            for (auto dialog : view->enumerate_views(false))
            {
                if ((dialog == view) || !dialog->is_visible())
                {
                    continue;
                }

                auto dialog_box = get_transformed_wm_geometry(dialog);
                if (dialog_box & max_geom)
                {
                    title.dialog = dialog;
                    break;
                }
            }
        }

        return view == title.dialog;
    }

    /**
     * Pre-render hook: calculates new position and optionally re-renders the text.
     */
    bool pre_render()
    {
        bool ret  = false;
        auto& tex = get_overlay_texture(find_toplevel_parent(view));
        if (!should_have_overlay(tex))
        {
            if (overlay_shown)
            {
                ret = true;
                overlay_shown = false;
            }

            view_padding = {0, 0, 0, 0};
            return ret;
        }

        if (!overlay_shown)
        {
            overlay_shown = true;
            ret = true;
        }

        auto box = get_parent_box(); // will return our box if there is no parent
        auto output_scale = parent.output->handle->scale;

        /**
         * regenerate the overlay texture in the following cases:
         * 1. Output's scale changed
         * 2. The overlay does not fit anymore
         * 3. The overlay previously did not fit, but there is more space now
         * TODO: check if this wastes too high CPU power when views are being
         * animated and maybe redraw less frequently
         */
        if ((tex.overlay.tex.tex == (GLuint) - 1) ||
            (output_scale != tex.par.output_scale) ||
            (tex.overlay.tex.width > box.width * output_scale) ||
            (tex.overflow &&
             (tex.overlay.tex.width < std::floor(box.width * output_scale))))
        {
            tex.par.output_scale = output_scale;
            tex.update_overlay_texture({box.width, box.height});
            ret = true;
        }

        int w = tex.overlay.tex.width;
        int h = tex.overlay.tex.height;
        int y = 0;
        switch (pos)
        {
          case position::TOP:
            y = box.y - (int)(h / output_scale);
            break;

          case position::CENTER:
            y = box.y + box.height / 2 - (int)(h / output_scale / 2);
            break;

          case position::BOTTOM:
            y = box.y + box.height;
            break;
        }

        geometry = {box.x + box.width / 2 - (int)(w / output_scale / 2),
            y, (int)(w / output_scale), (int)(h / output_scale)};

        /* We need to ensure that geometry is within our box. */
        if (view->parent || (pos != position::CENTER))
        {
            /* get out own box (previously we might have had the parent's box */
            if (view->parent)
            {
                box = get_transformed_wm_geometry();
            }

            view_padding = {0, 0, 0, 0};
            if (geometry.x < box.x)
            {
                view_padding.left = box.x - geometry.x;
            }

            if (geometry.x + geometry.width > box.x + box.width)
            {
                view_padding.right = (geometry.x + geometry.width) -
                    (box.x + box.width);
            }

            if (geometry.y < box.y)
            {
                view_padding.top = box.y - geometry.y;
            }

            if (geometry.y + geometry.height > box.y + box.height)
            {
                view_padding.bottom = (geometry.y + geometry.height) -
                    (box.y + box.height);
            }

            /* note: no need to call damage, the transformer will check if the
             * padding has changed and will damage the view accordingly */
        } else
        {
            view_padding = {0, 0, 0, 0};
        }

        return ret;
    }

    void render(const wf::framebuffer_t& fb, const wf::region_t& damage)
    {
        if (!overlay_shown)
        {
            return;
        }

        view_title_texture_t& title = get_overlay_texture(find_toplevel_parent(
            tr.get_transformed_view()));

        GLuint tex = title.overlay.tex.tex;

        if (tex == (GLuint) - 1)
        {
            /* this should not happen */
            return;
        }

        auto ortho = fb.get_orthographic_projection();
        OpenGL::render_begin(fb);
        for (const auto& box : damage)
        {
            fb.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::render_transformed_texture(tex, geometry, ortho,
                {1.0f, 1.0f, 1.0f, tr.alpha}, OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
        }

        OpenGL::render_end();
    }

  public:
    view_title_overlay_t(wf::scale_transformer_t& tr_, position pos_,
        scale_show_title_t& parent_) : tr(tr_), view(tr.get_transformed_view()),
        parent(parent_), pos(pos_)
    {
        auto parent = find_toplevel_parent(view);
        auto& title = get_overlay_texture(parent);

        if (title.overlay.tex.tex != (GLuint) - 1)
        {
            text_height = (unsigned int)std::ceil(
                title.overlay.tex.height / title.par.output_scale);
        } else
        {
            text_height =
                wf::cairo_text_t::measure_height(title.par.font_size, true);
        }

        /* add padding required by scale */
        if (pos == position::BOTTOM)
        {
            scale_padding.bottom = text_height;
        } else if (pos == position::TOP)
        {
            scale_padding.top = text_height;
        }

        pre_hook = [this] ()
        {
            return pre_render();
        };
        render_hook = [this] (
            const wf::framebuffer_t& fb,
            const wf::region_t& damage)
        {
            render(fb, damage);
        };
    }

    ~view_title_overlay_t()
    {
        view->erase_data<view_title_texture_t>();
        if (view->parent && overlay_shown)
        {
            auto parent = find_toplevel_parent(view);
            /* we need to recalculate which dialog should show the overlay */
            auto tmp = parent->get_transformer(
                wf::scale_transformer_t::transformer_name());
            auto tr = dynamic_cast<wf::scale_transformer_t*>(tmp.get());
            if (tr)
            {
                tr->call_pre_hooks(false);
            }
        }
    }

    view_title_overlay_t(const view_title_overlay_t &) = delete;
    view_title_overlay_t(view_title_overlay_t &&) = delete;
    view_title_overlay_t& operator =(const view_title_overlay_t&) = delete;
    view_title_overlay_t& operator =(view_title_overlay_t&&) = delete;
};

scale_show_title_t::scale_show_title_t() :
    view_filter{[this] (auto)
    {
        update_title_overlay_opt();
    }},

    scale_end{[this] (wf::signal_data_t*)
    {
        show_view_title_overlay = title_overlay_t::NEVER;
        last_title_overlay = nullptr;
        mouse_update.disconnect();
    }
},

add_title_overlay{[this] (wf::signal_data_t *data)
    {
        const std::string& opt = show_view_title_overlay_opt;
        if (opt == "never")
        {
            /* TODO: support changing this option while scale is running! */
            return;
        }

        const std::string& pos_opt = title_position;
        view_title_overlay_t::position pos = view_title_overlay_t::position::CENTER;
        if (pos_opt == "top")
        {
            pos = view_title_overlay_t::position::TOP;
        } else if (pos_opt == "bottom")
        {
            pos = view_title_overlay_t::position::BOTTOM;
        }

        auto signal = static_cast<scale_transformer_added_signal*>(data);
        auto tr     = signal->transformer;
        auto ol     = new view_title_overlay_t(*tr, pos, *this);

        tr->add_overlay(std::unique_ptr<wf::scale_transformer_t::overlay_t>(ol), 1);
    }
},

mouse_update{[this] (auto)
    {
        update_title_overlay_mouse();
    }
}

{}

void scale_show_title_t::init(wf::output_t *output)
{
    this->output = output;
    output->connect_signal("scale-filter", &view_filter);
    output->connect_signal("scale-transformer-added", &add_title_overlay);
    output->connect_signal("scale-end", &scale_end);
}

void scale_show_title_t::fini()
{
    mouse_update.disconnect();
}

void scale_show_title_t::update_title_overlay_opt()
{
    const std::string& tmp = show_view_title_overlay_opt;
    if (tmp == "all")
    {
        show_view_title_overlay = title_overlay_t::ALL;
    } else if (tmp == "mouse")
    {
        show_view_title_overlay = title_overlay_t::MOUSE;
    } else
    {
        show_view_title_overlay = title_overlay_t::NEVER;
    }

    if (show_view_title_overlay == title_overlay_t::MOUSE)
    {
        update_title_overlay_mouse();
        mouse_update.disconnect();
        wf::get_core().connect_signal("pointer_motion_absolute_post", &mouse_update);
        wf::get_core().connect_signal("pointer_motion_post", &mouse_update);
    }
}

void scale_show_title_t::update_title_overlay_mouse()
{
    wayfire_view v;

    wf::option_wrapper_t<bool> interact{"scale/interact"};

    if (interact)
    {
        /* we can use normal focus tracking */
        v = wf::get_core().get_cursor_focus().view();
    } else
    {
        auto& core = wf::get_core();
        v = core.get_surface_at(core.get_cursor_position()).view();
    }

    if (v)
    {
        v = find_toplevel_parent(v);

        if (v->role != wf::VIEW_ROLE_TOPLEVEL)
        {
            v = nullptr;
        }
    }

    if (v != last_title_overlay)
    {
        if (last_title_overlay)
        {
            last_title_overlay->damage();
        }

        last_title_overlay = v;
        if (v)
        {
            v->damage();
        }
    }
}
