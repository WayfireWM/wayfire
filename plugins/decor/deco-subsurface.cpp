#define GLM_FORCE_RADIANS
#include <glm/gtc/matrix_transform.hpp>

#include <linux/input-event-codes.h>

#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/core.hpp>
#include <wayfire/decorator.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include "deco-subsurface.hpp"
#include "deco-layout.hpp"
#include "deco-theme.hpp"

#include <wayfire/plugins/common/cairo-util.hpp>

#include <cairo.h>

class simple_decoration_surface : public wf::surface_interface_t,
    public wf::input_surface_t, public wf::output_surface_t
{
    bool _mapped = true;

    wayfire_view view;
    wf::signal_connection_t title_set = [=] (wf::signal_data_t *data)
    {
        if (get_signaled_view(data) == view)
        {
            view->damage(); // trigger re-render
        }
    };

    void update_title(int width, int height, double scale)
    {
        int target_width  = width * scale;
        int target_height = height * scale;

        if ((title_texture.tex.width != target_width) ||
            (title_texture.tex.height != target_height) ||
            (title_texture.current_text != view->get_title()))
        {
            auto surface = theme.render_text(view->get_title(),
                target_width, target_height);
            cairo_surface_upload_to_texture(surface, title_texture.tex);
            cairo_surface_destroy(surface);
            title_texture.current_text = view->get_title();
        }
    }

    struct
    {
        wf::simple_texture_t tex;
        std::string current_text = "";
    } title_texture;

    wf::decor::decoration_theme_t theme;
    wf::decor::decoration_layout_t layout;
    wf::region_t cached_region;

    wf::dimensions_t size;

  public:
    int current_thickness;
    int current_titlebar;

    simple_decoration_surface(wayfire_view view) :
        theme{},
        layout{theme, [=] (wlr_box box) { emit_damage({box}); }}
    {
        this->view = view;
        view->connect_signal("title-changed", &title_set);

        // make sure to hide frame if the view is fullscreen
        update_decoration_size();
    }

    /* wf::surface_interface_t implementation */
    virtual bool is_mapped() const final
    {
        return _mapped;
    }

    wf::point_t get_offset() final
    {
        return {-current_thickness, -current_titlebar};
    }

    virtual wf::dimensions_t get_size() const final
    {
        return size;
    }

    void render_title(const wf::framebuffer_t& fb,
        wf::geometry_t geometry)
    {
        update_title(geometry.width, geometry.height, fb.scale);
        OpenGL::render_texture(title_texture.tex.tex, fb, geometry,
            glm::vec4(1.0f), OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
    }

    void render_scissor_box(const wf::framebuffer_t& fb, wf::point_t origin,
        const wlr_box& scissor)
    {
        /* Clear background */
        wlr_box geometry{origin.x, origin.y, size.width, size.height};
        theme.render_background(fb, geometry, scissor, view->activated);

        /* Draw title & buttons */
        auto renderables = layout.get_renderable_areas();
        for (auto item : renderables)
        {
            if (item->get_type() == wf::decor::DECORATION_AREA_TITLE)
            {
                OpenGL::render_begin(fb);
                fb.logic_scissor(scissor);
                render_title(fb, item->get_geometry() + origin);
                OpenGL::render_end();
            } else // button
            {
                item->as_button().render(fb,
                    item->get_geometry() + origin, scissor);
            }
        }
    }

    virtual void simple_render(const wf::framebuffer_t& fb, wf::point_t pos,
        const wf::region_t& damage) override
    {
        wf::region_t frame = this->cached_region + pos;
        frame &= damage;

        for (const auto& box : frame)
        {
            render_scissor_box(fb, pos, wlr_box_from_pixman_box(box));
        }
    }

    void schedule_redraw(const timespec& frame_end) final
    {}

    void set_visible_on_output(wf::output_t *output, bool is_visible) final
    {}

    wf::region_t get_opaque_region() final
    {
        return {};
    }

    void handle_action(wf::decor::decoration_layout_t::action_response_t action)
    {
        switch (action.action)
        {
          case wf::decor::DECORATION_ACTION_MOVE:
            return view->move_request();

          case wf::decor::DECORATION_ACTION_RESIZE:
            return view->resize_request(action.edges);

          case wf::decor::DECORATION_ACTION_CLOSE:
            return view->close();

          case wf::decor::DECORATION_ACTION_TOGGLE_MAXIMIZE:
            if (view->tiled_edges)
            {
                view->tile_request(0);
            } else
            {
                view->tile_request(wf::TILED_EDGES_ALL);
            }

            break;

          case wf::decor::DECORATION_ACTION_MINIMIZE:
            view->minimize_request(true);
            break;

          default:
            break;
        }
    }

    bool accepts_input(wf::pointf_t at) override
    {
        return cached_region.contains_pointf(at);
    }

    std::optional<wf::region_t> handle_pointer_enter(wf::pointf_t at,
        bool refocus) override
    {
        layout.handle_motion(at.x, at.y);
        return {};
    }

    void handle_pointer_leave() override
    {
        layout.handle_focus_lost();
    }

    void handle_pointer_button(uint32_t time_ms, uint32_t button,
        wlr_button_state state) override
    {
        if (button != BTN_LEFT)
        {
            return;
        }

        handle_action(layout.handle_press_event(state == WLR_BUTTON_PRESSED));
    }

    void handle_pointer_motion(uint32_t time_ms, wf::pointf_t at) override
    {
        handle_action(layout.handle_motion(at.x, at.y));
    }

    void handle_pointer_axis(uint32_t time_ms,
        wlr_axis_orientation orientation, double delta,
        int32_t delta_discrete, wlr_axis_source source) override
    {}

    void handle_touch_down(uint32_t time_ms, int32_t id,
        wf::pointf_t at) override
    {
        layout.handle_motion(at.x, at.y);
        handle_action(layout.handle_press_event());
    }

    void handle_touch_up(uint32_t time_ms, int32_t id,
        bool finger_lifted) override
    {
        handle_action(layout.handle_press_event(false));
        layout.handle_focus_lost();
    }

    void handle_touch_motion(uint32_t time_ms, int32_t id,
        wf::pointf_t at) override
    {
        handle_action(layout.handle_motion(at.x, at.y));
    }

    wf::input_surface_t& input() override
    {
        return *this;
    }

    wf::output_surface_t& output() override
    {
        return *this;
    }

    void unmap()
    {
        _mapped = false;
        wf::emit_map_state_change(this);
    }

    void resize(wf::dimensions_t dims)
    {
        view->damage();
        size = dims;
        layout.resize(size.width, size.height);
        if (!view->fullscreen)
        {
            this->cached_region = layout.calculate_region();
        }

        view->damage();
    }

    void update_decoration_size()
    {
        if (view->fullscreen)
        {
            current_thickness = 0;
            current_titlebar  = 0;
            this->cached_region.clear();
        } else
        {
            current_thickness = theme.get_border_size();
            current_titlebar  =
                theme.get_title_height() + theme.get_border_size();
            this->cached_region = layout.calculate_region();
        }
    }
};

class simple_decorator_t : public wf::decorator_frame_t_t
{
    wayfire_view view;
    nonstd::observer_ptr<simple_decoration_surface> deco;

  public:
    simple_decorator_t(wayfire_view view)
    {
        this->view = view;

        auto sub = std::make_unique<simple_decoration_surface>(view);
        deco = {sub};
        view->get_main_surface()->add_subsurface(std::move(sub), true);
        view->damage();
        view->connect_signal("subsurface-removed", &on_subsurface_removed);
    }

    ~simple_decorator_t()
    {
        if (deco)
        {
            // subsurface_removed unmaps it
            view->get_main_surface()->remove_subsurface(deco);
        }
    }

    simple_decorator_t(const simple_decorator_t &) = delete;
    simple_decorator_t(simple_decorator_t &&) = delete;
    simple_decorator_t& operator =(const simple_decorator_t&) = delete;
    simple_decorator_t& operator =(simple_decorator_t&&) = delete;

    wf::signal_connection_t on_subsurface_removed = [&] (auto data)
    {
        auto ev = static_cast<wf::subsurface_removed_signal*>(data);
        if (ev->subsurface.get() == deco.get())
        {
            deco->unmap();
            deco = nullptr;
        }
    };

    /* frame implementation */
    virtual wf::geometry_t expand_wm_geometry(
        wf::geometry_t contained_wm_geometry) override
    {
        contained_wm_geometry.x     -= deco->current_thickness;
        contained_wm_geometry.y     -= deco->current_titlebar;
        contained_wm_geometry.width += 2 * deco->current_thickness;
        contained_wm_geometry.height +=
            deco->current_thickness + deco->current_titlebar;

        return contained_wm_geometry;
    }

    virtual void calculate_resize_size(
        int& target_width, int& target_height) override
    {
        target_width  -= 2 * deco->current_thickness;
        target_height -= deco->current_thickness + deco->current_titlebar;

        target_width  = std::max(target_width, 1);
        target_height = std::max(target_height, 1);
    }

    virtual void notify_view_activated(bool active) override
    {
        view->damage();
    }

    virtual void notify_view_resized(wf::geometry_t view_geometry) override
    {
        deco->resize(wf::dimensions(view_geometry));
    }

    virtual void notify_view_tiled() override
    {}

    virtual void notify_view_fullscreen() override
    {
        deco->update_decoration_size();

        if (!view->fullscreen)
        {
            notify_view_resized(view->get_wm_geometry());
        }
    }
};

void init_view(wayfire_view view)
{
    auto decor = std::make_unique<simple_decorator_t>(view);
    view->set_decoration(std::move(decor));
}

void deinit_view(wayfire_view view)
{
    view->set_decoration(nullptr);
}
