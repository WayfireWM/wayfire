#include "view/view-impl.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/workspace-set.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/view.hpp"
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/signal-definitions.hpp>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>
#include <wayfire/signal-provider.hpp>

static void render_colored_rect(const wf::render_target_t& fb,
    int x, int y, int w, int h, const wf::color_t& color)
{
    wf::color_t premultiply{color.r * color.a, color.g * color.a, color.b * color.a, color.a};
    OpenGL::render_rectangle({x, y, w, h}, premultiply, fb.get_orthographic_projection());
}

class wf::color_rect_view_t::color_rect_node_t : public wf::scene::floating_inner_node_t
{
    class color_rect_render_instance_t : public wf::scene::simple_render_instance_t<color_rect_node_t>
    {
      public:
        using simple_render_instance_t::simple_render_instance_t;
        void render(const wf::render_target_t& target, const wf::region_t& region) override
        {
            auto view = self->_view.lock();
            if (!view)
            {
                return;
            }

            auto geometry = self->get_bounding_box();
            auto border   = view->border;
            auto _border_color = view->_border_color;
            auto _color = view->_color;

            OpenGL::render_begin(target);
            for (const auto& box : region)
            {
                target.logic_scissor(wlr_box_from_pixman_box(box));

                /* Draw the border, making sure border parts don't overlap, otherwise
                 * we will get wrong corners if border has alpha != 1.0 */
                // top
                render_colored_rect(target, geometry.x, geometry.y, geometry.width, border,
                    _border_color);
                // bottom
                render_colored_rect(target, geometry.x, geometry.y + geometry.height - border,
                    geometry.width, border, _border_color);
                // left
                render_colored_rect(target, geometry.x, geometry.y + border, border,
                    geometry.height - 2 * border, _border_color);
                // right
                render_colored_rect(target, geometry.x + geometry.width - border,
                    geometry.y + border, border, geometry.height - 2 * border, _border_color);

                /* Draw the inside of the rect */
                render_colored_rect(target, geometry.x + border, geometry.y + border,
                    geometry.width - 2 * border, geometry.height - 2 * border, _color);
            }

            OpenGL::render_end();
        }
    };

    std::weak_ptr<color_rect_view_t> _view;

  public:
    color_rect_node_t(std::weak_ptr<color_rect_view_t> view) : scene::floating_inner_node_t(false)
    {
        _view = view;
    }

    void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
        scene::damage_callback push_damage, wf::output_t *output) override
    {
        instances.push_back(std::make_unique<color_rect_render_instance_t>(this, push_damage, output));
    }

    wf::geometry_t get_bounding_box() override
    {
        if (auto view = _view.lock())
        {
            return view->get_geometry();
        } else
        {
            return {0, 0, 0, 0};
        }
    }
};

/* Implementation of color_rect_view_t */
wf::color_rect_view_t::color_rect_view_t() : wf::view_interface_t()
{
    this->geometry = {0, 0, 1, 1};
    this->_color   = {0, 0, 0, 1};
    this->border   = 0;
}

std::shared_ptr<wf::color_rect_view_t> wf::color_rect_view_t::create(view_role_t role,
    wf::output_t *start_output, std::optional<wf::scene::layer> layer)
{
    auto self = view_interface_t::create<wf::color_rect_view_t>();
    self->set_surface_root_node(std::make_shared<color_rect_node_t>(self));
    self->set_role(role);

    self->_is_mapped = true;
    self->get_root_node()->set_enabled(true);

    if (start_output)
    {
        self->set_output(start_output);

        if (layer)
        {
            auto parent = (layer == wf::scene::layer::WORKSPACE) ?
                start_output->wset()->get_node() : start_output->node_for_layer(*layer);
            wf::scene::readd_front(parent, self->get_root_node());
        }
    }

    wf::view_implementation::emit_view_map_signal(self, true);
    return self;
}

void wf::color_rect_view_t::close()
{
    this->_is_mapped = false;
    emit_view_unmap();
}

void wf::color_rect_view_t::set_color(wf::color_t color)
{
    this->_color = color;
    damage();
}

void wf::color_rect_view_t::set_border_color(wf::color_t border)
{
    this->_border_color = border;
    damage();
}

void wf::color_rect_view_t::set_border(int width)
{
    this->border = width;
    damage();
}

bool wf::color_rect_view_t::is_mapped() const
{
    return _is_mapped;
}

void wf::color_rect_view_t::set_geometry(wf::geometry_t g)
{
    damage();
    this->geometry = g;
    damage();
}

wf::geometry_t wf::color_rect_view_t::get_geometry()
{
    return this->geometry;
}

wlr_surface*wf::color_rect_view_t::get_keyboard_focus_surface()
{
    return nullptr;
}

bool wf::color_rect_view_t::is_focusable() const
{
    return false;
}
