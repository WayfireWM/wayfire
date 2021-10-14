#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/signal-definitions.hpp>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

/* Implementation of mirror_view_t */
wf::mirror_view_t::mirror_view_t(wayfire_view base_view) :
    wf::view_interface_t()
{
    this->base_view = base_view;

    base_view_unmapped = [=] (wf::signal_data_t*)
    {
        close();
    };

    base_view->connect_signal("unmapped", &base_view_unmapped);

    base_view_damaged = [=] (wf::signal_data_t*)
    {
        damage();
    };

    base_view->connect_signal("region-damaged", &base_view_damaged);
}

wf::mirror_view_t::~mirror_view_t()
{}

void wf::mirror_view_t::close()
{
    if (!base_view)
    {
        return;
    }

    emit_view_pre_unmap();

    base_view->disconnect_signal("unmapped", &base_view_unmapped);
    base_view->disconnect_signal("region-damaged", &base_view_damaged);
    base_view = nullptr;

    emit_map_state_change(this);
    emit_view_unmap();

    unref();
}

bool wf::mirror_view_t::is_mapped() const
{
    return base_view && base_view->is_mapped();
}

wf::dimensions_t wf::mirror_view_t::get_size() const
{
    if (!is_mapped())
    {
        return {0, 0};
    }

    auto box = base_view->get_bounding_box();

    return {box.width, box.height};
}

void wf::mirror_view_t::simple_render(const wf::framebuffer_t& fb, int x, int y,
    const wf::region_t& damage)
{
    if (!is_mapped())
    {
        return;
    }

    /* Normally we shouldn't copy framebuffers. But in this case we can assume
     * nothing will break, because the copy will be destroyed immediately */
    wf::framebuffer_t copy;
    std::memcpy((void*)&copy, (void*)&fb, sizeof(wf::framebuffer_t));

    /* The base view is in another coordinate system, we need to calculate the
     * difference between the two, so that it appears at the correct place.
     *
     * Note that this simply means we need to change fb's geometry. Damage is
     * calculated for this mirror view, and needs to stay as it is */
    auto base_bbox = base_view->get_bounding_box();

    wf::point_t offset = {base_bbox.x - x, base_bbox.y - y};
    copy.geometry = copy.geometry + offset;
    base_view->render_transformed(copy, damage + offset);
    copy.reset();
}

void wf::mirror_view_t::move(int x, int y)
{
    damage();
    view_geometry_changed_signal data;
    data.old_geometry = get_wm_geometry();

    this->x = x;
    this->y = y;

    damage();
    emit_signal("geometry-changed", &data);
}

wf::geometry_t wf::mirror_view_t::get_output_geometry()
{
    if (!is_mapped())
    {
        return get_bounding_box();
    }

    wf::geometry_t geometry;
    geometry.x = this->x;
    geometry.y = this->y;

    auto dims = get_size();
    geometry.width  = dims.width;
    geometry.height = dims.height;

    return geometry;
}

wlr_surface*wf::mirror_view_t::get_keyboard_focus_surface()
{
    return nullptr;
}

bool wf::mirror_view_t::is_focuseable() const
{
    return false;
}

bool wf::mirror_view_t::should_be_decorated()
{
    return false;
}

/* Implementation of color_rect_view_t */

wf::color_rect_view_t::color_rect_view_t() : wf::view_interface_t()
{
    this->geometry   = {0, 0, 1, 1};
    this->_color     = {0, 0, 0, 1};
    this->border     = 0;
    this->_is_mapped = true;
}

void wf::color_rect_view_t::close()
{
    this->_is_mapped = false;

    emit_view_unmap();
    emit_map_state_change(this);

    unref();
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

wf::dimensions_t wf::color_rect_view_t::get_size() const
{
    return {
        geometry.width,
        geometry.height,
    };
}

static void render_colored_rect(const wf::framebuffer_t& fb,
    int x, int y, int w, int h, const wf::color_t& color)
{
    wf::color_t premultiply{
        color.r * color.a,
        color.g * color.a,
        color.b * color.a,
        color.a};

    OpenGL::render_rectangle({x, y, w, h}, premultiply,
        fb.get_orthographic_projection());
}

void wf::color_rect_view_t::simple_render(const wf::framebuffer_t& fb, int x, int y,
    const wf::region_t& damage)
{
    OpenGL::render_begin(fb);
    for (const auto& box : damage)
    {
        fb.logic_scissor(wlr_box_from_pixman_box(box));

        /* Draw the border, making sure border parts don't overlap, otherwise
         * we will get wrong corners if border has alpha != 1.0 */
        // top
        render_colored_rect(fb, x, y, geometry.width, border,
            _border_color);
        // bottom
        render_colored_rect(fb, x, y + geometry.height - border,
            geometry.width, border, _border_color);
        // left
        render_colored_rect(fb, x, y + border, border,
            geometry.height - 2 * border, _border_color);
        // right
        render_colored_rect(fb, x + geometry.width - border,
            y + border, border, geometry.height - 2 * border, _border_color);

        /* Draw the inside of the rect */
        render_colored_rect(fb, x + border, y + border,
            geometry.width - 2 * border, geometry.height - 2 * border,
            _color);
    }

    OpenGL::render_end();
}

void wf::color_rect_view_t::move(int x, int y)
{
    damage();
    view_geometry_changed_signal data;
    data.old_geometry = get_wm_geometry();

    this->geometry.x = x;
    this->geometry.y = y;

    damage();
    emit_signal("geometry-changed", &data);
}

void wf::color_rect_view_t::resize(int w, int h)
{
    damage();
    view_geometry_changed_signal data;
    data.old_geometry = get_wm_geometry();

    this->geometry.width  = w;
    this->geometry.height = h;

    damage();
    emit_signal("geometry-changed", &data);
}

wf::geometry_t wf::color_rect_view_t::get_output_geometry()
{
    return geometry;
}

wlr_surface*wf::color_rect_view_t::get_keyboard_focus_surface()
{
    return nullptr;
}

bool wf::color_rect_view_t::is_focuseable() const
{
    return false;
}

bool wf::color_rect_view_t::should_be_decorated()
{
    return false;
}

void wf::no_input_surface_t::handle_pointer_axis(uint32_t time_ms,
    wlr_axis_orientation orientation, double delta, int32_t delta_discrete,
    wlr_axis_source source)
{}

void wf::no_input_surface_t::handle_pointer_motion(
    uint32_t time_ms, wf::pointf_t at)
{}

void wf::no_input_surface_t::handle_pointer_button(
    uint32_t time_ms, uint32_t button, wlr_button_state state)
{}

void wf::no_input_surface_t::handle_pointer_leave()
{}

std::optional<wf::region_t> wf::no_input_surface_t::handle_pointer_enter(
    wf::pointf_t at, bool refocus)
{
    return {};
}

void wf::no_input_surface_t::handle_touch_down(
    uint32_t time_ms, int32_t id, wf::pointf_t at)
{}

void wf::no_input_surface_t::handle_touch_up(
    uint32_t time_ms, int32_t id, bool finger_lifted)
{}

void wf::no_input_surface_t::handle_touch_motion(
    uint32_t time_ms, int32_t id, wf::pointf_t at)
{}

bool wf::no_input_surface_t::accepts_input(wf::pointf_t at)
{
    return false;
}

wf::input_surface_t& wf::color_rect_view_t::input()
{
    return *this;
}

wf::input_surface_t& wf::mirror_view_t::input()
{
    return *this;
}
