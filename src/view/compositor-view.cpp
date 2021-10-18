#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/signal-definitions.hpp>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

/* Implementation of mirror_view_t */
wf::mirror_view_t::mirror_view_t(wayfire_view base_view) : wf::view_interface_t(
        nullptr)
{
    this->mirror_surface = std::make_shared<wf::mirror_surface_t>(base_view);
    this->set_main_surface(mirror_surface);

    this->base_view = base_view;

    on_source_view_unmapped.set_callback([=] (wf::signal_data_t*)
    {
        this->base_view = nullptr;
        close();
    });

    base_view->connect_signal("unmapped", &on_source_view_unmapped);
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

    on_source_view_unmapped.disconnect();
    base_view = nullptr;

    emit_map_state_change(this->get_main_surface().get());
    emit_view_unmap();

    unref();
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

    auto dims = mirror_surface->output().get_size();
    geometry.width  = dims.width;
    geometry.height = dims.height;

    return geometry;
}

bool wf::mirror_view_t::is_focuseable() const
{
    return false;
}

bool wf::mirror_view_t::should_be_decorated()
{
    return false;
}

wf::mirror_surface_t::mirror_surface_t(wayfire_view source)
{
    this->base_view = source;

    on_source_view_damaged.set_callback([=] (wf::signal_data_t*)
    {
        emit_damage(wf::region_t{{0, 0, get_size().width, get_size().height}});
    });

    base_view->connect_signal("region-damaged", &on_source_view_damaged);

    on_source_view_unmapped.set_callback([=] (wf::signal_data_t*)
    {
        this->base_view = nullptr;
    });
    base_view->connect_signal("unmapped", &on_source_view_unmapped);
}

bool wf::mirror_surface_t::is_mapped() const
{
    return base_view && base_view->is_mapped();
}

wf::input_surface_t& wf::mirror_surface_t::input()
{
    return *this;
}

wf::output_surface_t& wf::mirror_surface_t::output()
{
    return *this;
}

wf::point_t wf::mirror_surface_t::get_offset()
{
    return {0, 0};
}

wf::dimensions_t wf::mirror_surface_t::get_size() const
{
    if (!base_view)
    {
        return {0, 0};
    }

    auto box = base_view->get_bounding_box();
    return {box.width, box.height};
}

void wf::mirror_surface_t::schedule_redraw(const timespec& frame_end)
{}

void wf::mirror_surface_t::set_visible_on_output(
    wf::output_t *output, bool is_visible)
{
    /* no-op */ }

wf::region_t wf::mirror_surface_t::get_opaque_region()
{
    return {};
}

void wf::mirror_surface_t::simple_render(
    const wf::framebuffer_t& fb, wf::point_t pos, const wf::region_t& damage)
{
    if (!base_view)
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

    wf::point_t offset = wf::origin(base_bbox) - pos;
    copy.geometry = copy.geometry + offset;
    base_view->render_transformed(copy, damage + offset);
    copy.reset();
}

/* Implementation of color_rect_view_t */

wf::color_rect_view_t::color_rect_view_t() : wf::view_interface_t(nullptr)
{
    this->color_surface = std::make_shared<wf::solid_bordered_surface_t>();
    this->set_main_surface(color_surface);
    this->_is_mapped = true;
}

void wf::color_rect_view_t::close()
{
    emit_view_unmap();
    color_surface->unmap();
    unref();
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

void wf::color_rect_view_t::move(int x, int y)
{
    damage();
    view_geometry_changed_signal data;
    data.old_geometry = get_wm_geometry();
    this->position    = {x, y};

    damage();
    emit_signal("geometry-changed", &data);
}

void wf::color_rect_view_t::resize(int w, int h)
{
    view_geometry_changed_signal data;
    data.old_geometry = get_wm_geometry();

    // This will also apply the damage
    color_surface->set_size({w, h});

    emit_signal("geometry-changed", &data);
}

wf::geometry_t wf::color_rect_view_t::get_output_geometry()
{
    return wf::construct_box(position, color_surface->output().get_size());
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

void wf::solid_bordered_surface_t::set_border(int width)
{
    this->border = width;
    emit_damage(wf::construct_box({0, 0}, get_size()));
}

void wf::solid_bordered_surface_t::set_border_color(wf::color_t border)
{
    this->_border_color = border;
    emit_damage(wf::construct_box({0, 0}, get_size()));
}

void wf::solid_bordered_surface_t::set_color(wf::color_t color)
{
    this->_color = color;
    emit_damage(wf::construct_box({0, 0}, get_size()));
}

void wf::solid_bordered_surface_t::set_size(wf::dimensions_t new_size)
{
    emit_damage(wf::construct_box({0, 0}, get_size()));
    this->size = new_size;
    emit_damage(wf::construct_box({0, 0}, get_size()));
}

wf::point_t wf::solid_bordered_surface_t::get_offset()
{
    return {0, 0};
}

wf::dimensions_t wf::solid_bordered_surface_t::get_size() const
{
    return size;
}

void wf::solid_bordered_surface_t::schedule_redraw(const timespec& frame_end)
{}

void wf::solid_bordered_surface_t::set_visible_on_output(
    wf::output_t *output, bool is_visible)
{
    /* no-op */ }

wf::region_t wf::solid_bordered_surface_t::get_opaque_region()
{
    return {};
}

void wf::solid_bordered_surface_t::simple_render(
    const wf::framebuffer_t& fb, wf::point_t pos, const wf::region_t& damage)
{
    OpenGL::render_begin(fb);
    for (const auto& box : damage)
    {
        fb.logic_scissor(wlr_box_from_pixman_box(box));

        /* Draw the border, making sure border parts don't overlap, otherwise
         * we will get wrong corners if border has alpha != 1.0 */
        // top
        render_colored_rect(fb, pos.x, pos.y, size.width, border,
            _border_color);
        // bottom
        render_colored_rect(fb, pos.x, pos.y + size.height - border,
            size.width, border, _border_color);
        // left
        render_colored_rect(fb, pos.x, pos.y + border, border,
            size.height - 2 * border, _border_color);
        // right
        render_colored_rect(fb, pos.x + size.width - border,
            pos.y + border, border, size.height - 2 * border, _border_color);

        /* Draw the inside of the rect */
        render_colored_rect(fb, pos.x + border, pos.y + border,
            size.width - 2 * border, size.height - 2 * border, _color);
    }

    OpenGL::render_end();
}

bool wf::no_input_view_t::accepts_focus() const
{
    return false;
}

void wf::no_input_view_t::handle_keyboard_enter()
{}

void wf::no_input_view_t::handle_keyboard_leave()
{}

void wf::no_input_view_t::handle_keyboard_key(wlr_event_keyboard_key event)
{}

wf::keyboard_focus_view_t& wf::color_rect_view_t::get_keyboard_focus()
{
    return *this;
}

wf::keyboard_focus_view_t& wf::mirror_view_t::get_keyboard_focus()
{
    return *this;
}

bool wf::solid_bordered_surface_t::is_mapped() const
{
    return mapped;
}

wf::input_surface_t& wf::solid_bordered_surface_t::input()
{
    return *this;
}

wf::output_surface_t& wf::solid_bordered_surface_t::output()
{
    return *this;
}

nonstd::observer_ptr<wf::solid_bordered_surface_t> wf::color_rect_view_t::
get_color_surface() const
{
    return {this->color_surface.get()};
}

void wf::solid_bordered_surface_t::unmap()
{
    this->mapped = false;
    emit_map_state_change(this);
}
