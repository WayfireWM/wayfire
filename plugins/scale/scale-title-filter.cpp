#include "wayfire/plugins/common/shared-core-data.hpp"
#include "wayfire/util.hpp"
#include <string>
#include <map>
#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/plugins/scale-signal.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/seat.hpp>

#include <linux/input-event-codes.h>

#include <wayfire/render-manager.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/plugins/common/simple-texture.hpp>
#include <wayfire/plugins/common/key-repeat.hpp>

class scale_title_filter;

/**
 * Class storing the filter text, shared among all outputs
 */
struct scale_title_filter_text
{
    std::string title_filter;
    /* since title filter is utf-8, here we store the length of each
     * character when adding them so backspace will work properly */
    std::vector<int> char_len;
    /* Individual plugins running on each output -- this is used to update them
     * when the shared filter text changes. */
    std::vector<scale_title_filter*> output_instances;

    void add_instance(scale_title_filter *ptr)
    {
        output_instances.push_back(ptr);
    }

    void rem_instance(scale_title_filter *ptr)
    {
        auto it = std::remove(output_instances.begin(), output_instances.end(),
            ptr);
        output_instances.erase(it, output_instances.end());
    }

    /**
     * Add any character corresponding to the given keycode to the filter.
     *
     * Updates the overlays and filter on all outputs if necessary.
     */
    void add_key(struct xkb_state *xkb_state, xkb_keycode_t keycode);

    /**
     * Remove the last character from the overlay.
     *
     * Updates the overlays and filter on all outputs if necessary.
     */
    void rem_char();

    /**
     * Check if scale has ended on all outputs and clears the filter in this case.
     */
    void check_scale_end();

    /**
     * Clear the current filter text. Does not update output-specific instances.
     */
    void clear()
    {
        title_filter.clear();
        char_len.clear();
    }
};

class scale_title_filter : public wf::per_output_plugin_instance_t
{
    wf::option_wrapper_t<bool> case_sensitive{"scale-title-filter/case_sensitive"};
    wf::option_wrapper_t<bool> share_filter{"scale-title-filter/share_filter"};
    scale_title_filter_text local_filter;
    wf::shared_data::ref_ptr_t<scale_title_filter_text> global_filter;

    inline void fix_case(std::string& string)
    {
        if (case_sensitive)
        {
            return;
        }

        auto transform = [] (unsigned char c) -> unsigned char
        {
            if (std::isspace(c))
            {
                return ' ';
            }

            return (c <= 127) ? (unsigned char)std::tolower(c) : c;
        };
        std::transform(string.begin(), string.end(), string.begin(), transform);
    }

    bool should_show_view(wayfire_view view)
    {
        auto filter = get_active_filter().title_filter;

        if (filter.empty())
        {
            return true;
        }

        auto title  = view->get_title();
        auto app_id = view->get_app_id();

        fix_case(title);
        fix_case(app_id);
        fix_case(filter);

        return (title.find(filter) != std::string::npos) ||
               (app_id.find(filter) != std::string::npos);
    }

    scale_title_filter_text& get_active_filter()
    {
        return share_filter ? *global_filter.get() : local_filter;
    }

  public:
    bool scale_running = false;

    scale_title_filter()
    {
        local_filter.add_instance(this);
    }

    void init() override
    {
        global_filter->add_instance(this);
        share_filter.set_callback(shared_option_changed);
        output->connect(&view_filter);
        output->connect(&scale_end);
    }

    void fini() override
    {
        do_end_scale();
        global_filter->rem_instance(this);
    }

    wf::signal::connection_t<scale_filter_signal> view_filter = [=] (scale_filter_signal *ev)
    {
        if (!scale_running)
        {
            wf::get_core().connect(&scale_key);
            scale_running = true;
            update_overlay();
        }

        scale_filter_views(ev, [this] (wayfire_toplevel_view v)
        {
            return !should_show_view(v);
        });
    };

    std::map<uint32_t, std::unique_ptr<wf::key_repeat_t>> keys;
    wf::key_repeat_t::callback_t handle_key_repeat = [=] (uint32_t raw_keycode)
    {
        auto seat     = wf::get_core().get_current_seat();
        auto keyboard = wlr_seat_get_keyboard(seat);
        if (!keyboard)
        {
            return false; /* should not happen */
        }

        auto xkb_state = keyboard->xkb_state;
        xkb_keycode_t keycode = raw_keycode + 8;
        xkb_keysym_t keysym   = xkb_state_key_get_one_sym(xkb_state, keycode);
        auto& filter = get_active_filter();
        if (keysym == XKB_KEY_BackSpace)
        {
            filter.rem_char();
        } else
        {
            filter.add_key(xkb_state, keycode);
        }

        return true;
    };

    wf::wl_idle_call idle_update_filter;

    void update_filter()
    {
        // Delay updating the filter in case the last key causes scale to exit.
        idle_update_filter.run_once([&]
        {
            if (scale_running)
            {
                scale_update_signal ev;
                output->emit(&ev);
                update_overlay();
            }
        });
    }

    wf::signal::connection_t<wf::input_event_signal<wlr_keyboard_key_event>> scale_key =
        [this] (wf::input_event_signal<wlr_keyboard_key_event> *ev)
    {
        if (ev->event->state == WL_KEYBOARD_KEY_STATE_RELEASED)
        {
            keys.erase(ev->event->keycode);
            return;
        }

        if ((ev->event->keycode == KEY_ESC) || (ev->event->keycode == KEY_ENTER))
        {
            return;
        }

        if (output != wf::get_core().seat->get_active_output())
        {
            return;
        }

        keys[ev->event->keycode] = std::make_unique<wf::key_repeat_t>(ev->event->keycode, handle_key_repeat);
        handle_key_repeat(ev->event->keycode);
    };


    wf::signal::connection_t<scale_end_signal> scale_end = [=] (scale_end_signal *ev)
    {
        do_end_scale();
    };

    void do_end_scale()
    {
        scale_key.disconnect();
        keys.clear();
        clear_overlay();
        scale_running = false;
        get_active_filter().check_scale_end();
    }

    wf::config::option_base_t::updated_callback_t shared_option_changed = [this] ()
    {
        if (scale_running)
        {
            /* clear the filter that is not used anymore */
            auto& filter = get_active_filter();
            filter.clear();

            scale_update_signal ev;
            output->emit(&ev);
            update_overlay();
        }
    };

  protected:
    /*
     * Text overlay with the current filter
     */
    wf::cairo_text_t filter_overlay;
    wf::dimensions_t overlay_size;
    float output_scale = 1.0f;
    /* render function */
    wf::effect_hook_t render_hook = [=] () { render(); };
    /* flag to indicate if render_hook is active */
    bool render_active = false;
    wf::option_wrapper_t<wf::color_t> bg_color{"scale-title-filter/bg_color"};
    wf::option_wrapper_t<wf::color_t> text_color{"scale-title-filter/text_color"};
    wf::option_wrapper_t<bool> show_overlay{"scale-title-filter/overlay"};
    wf::option_wrapper_t<int> font_size{"scale-title-filter/font_size"};

    static wf::dimensions_t min(const wf::dimensions_t& x, const wf::dimensions_t& y)
    {
        return {std::min(x.width, y.width), std::min(x.height, y.height)};
    }

    static wf::dimensions_t max(const wf::dimensions_t& x, const wf::dimensions_t& y)
    {
        return {std::max(x.width, y.width), std::max(x.height, y.height)};
    }

    void update_overlay()
    {
        const auto& filter = get_active_filter().title_filter;

        if (!show_overlay || filter.empty())
        {
            /* remove any overlay */
            clear_overlay();
            return;
        }

        auto dim = output->get_screen_size();
        auto new_size = filter_overlay.render_text(filter,
            wf::cairo_text_t::params(font_size, bg_color, text_color, output_scale,
                dim));

        if (!render_active)
        {
            output->render->add_effect(&render_hook, wf::OUTPUT_EFFECT_OVERLAY);
            render_active = true;
        }

        auto surface_size = min(new_size, {filter_overlay.tex.width,
            filter_overlay.tex.height});
        auto damage = max(surface_size, overlay_size);

        output->render->damage({
            dim.width / 2 - (int)(damage.width / output_scale / 2),
            dim.height / 2 - (int)(damage.height / output_scale / 2),
            (int)(damage.width / output_scale),
            (int)(damage.height / output_scale)
        });

        overlay_size = surface_size;
    }

    /* render the current content of the overlay texture */
    void render()
    {
        auto out_fb = output->render->get_target_framebuffer();
        auto dim    = output->get_screen_size();
        if (output_scale != out_fb.scale)
        {
            output_scale = out_fb.scale;
            update_overlay();
        }

        const wf::simple_texture_t& tex = filter_overlay.tex;
        if (tex.tex == (GLuint) - 1)
        {
            return;
        }

        wf::geometry_t geometry{
            dim.width / 2 - (int)(overlay_size.width / output_scale / 2),
            dim.height / 2 - (int)(overlay_size.height / output_scale / 2),
            (int)(overlay_size.width / output_scale),
            (int)(overlay_size.height / output_scale)
        };
        gl_geometry gl_geom{(float)geometry.x, (float)geometry.y,
            (float)(geometry.x + geometry.width),
            (float)(geometry.y + geometry.height)};
        float tex_wr = (float)overlay_size.width / (float)tex.width;
        float tex_hr = (float)overlay_size.height / (float)tex.height;
        gl_geometry tex_geom{0.5f - tex_wr / 2.f, 0.5f - tex_hr / 2.f,
            0.5f + tex_wr / 2.f, 0.5f + tex_hr / 2.f};

        auto damage = output->render->get_scheduled_damage() & geometry;
        auto ortho  = out_fb.get_orthographic_projection();

        OpenGL::render_begin(out_fb);
        for (auto& box : damage)
        {
            out_fb.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::render_transformed_texture(tex.tex, gl_geom, tex_geom, ortho,
                glm::vec4(1.f),
                OpenGL::TEXTURE_TRANSFORM_INVERT_Y |
                OpenGL::TEXTURE_USE_TEX_GEOMETRY);
        }

        OpenGL::render_end();
    }

    /* clear everything rendered by this plugin and deactivate rendering */
    void clear_overlay()
    {
        if (render_active)
        {
            output->render->rem_effect(&render_hook);
            auto dim = output->get_screen_size();
            int surface_width  = filter_overlay.tex.width;
            int surface_height = filter_overlay.tex.height;

            output->render->damage({
                dim.width / 2 - (int)(surface_width / output_scale / 2),
                dim.height / 2 - (int)(surface_height / output_scale / 2),
                (int)(surface_width / output_scale),
                (int)(surface_height / output_scale)
            });
            render_active = false;
        }
    }
};

void scale_title_filter_text::add_key(struct xkb_state *xkb_state,
    xkb_keycode_t keycode)
{
    /* taken from libxkbcommon guide */
    int size = xkb_state_key_get_utf8(xkb_state, keycode, nullptr, 0);
    if (size <= 0)
    {
        return;
    }

    std::string tmp(size, 0);
    xkb_state_key_get_utf8(xkb_state, keycode, tmp.data(), size + 1);
    char_len.push_back(size);
    title_filter += tmp;

    for (auto p : output_instances)
    {
        p->update_filter();
    }
}

void scale_title_filter_text::rem_char()
{
    if (!title_filter.empty())
    {
        int len = char_len.back();
        char_len.pop_back();
        title_filter.resize(title_filter.length() - len);
    } else
    {
        return;
    }

    for (auto p : output_instances)
    {
        p->update_filter();
    }
}

void scale_title_filter_text::check_scale_end()
{
    bool scale_running = false;
    for (auto p : output_instances)
    {
        if (p->scale_running)
        {
            scale_running = true;
            break;
        }
    }

    if (!scale_running)
    {
        clear();
    }
}

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<scale_title_filter>);
