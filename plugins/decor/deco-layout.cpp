#include "deco-layout.hpp"
#include "deco-theme.hpp"
#include <wayfire/core.hpp>
#include <wayfire/nonstd/reverse.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/util.hpp>

#define BUTTON_HEIGHT_PC 0.7

namespace wf
{
namespace decor
{
/**
 * Represents an area of the decoration which reacts to input events.
 */
decoration_area_t::decoration_area_t(decoration_area_type_t type, wf::geometry_t g)
{
    this->type     = type;
    this->geometry = g;

    assert(type != DECORATION_AREA_BUTTON);
}

/**
 * Initialize a new decoration area holding a button
 */
decoration_area_t::decoration_area_t(wf::geometry_t g,
    std::function<void(wlr_box)> damage_callback,
    const decoration_theme_t& theme)
{
    this->type     = DECORATION_AREA_BUTTON;
    this->geometry = g;

    this->button = std::make_unique<button_t>(theme,
        std::bind(damage_callback, g));
}

wf::geometry_t decoration_area_t::get_geometry() const
{
    return geometry;
}

button_t& decoration_area_t::as_button()
{
    assert(button);

    return *button;
}

decoration_area_type_t decoration_area_t::get_type() const
{
    return type;
}

decoration_layout_t::decoration_layout_t(const decoration_theme_t& th,
    std::function<void(wlr_box)> callback) :

    titlebar_size(th.get_title_height()),
    border_size(th.get_border_size()),
    /**
     * This is necessary. Otherwise, we will draw an
     * overly huge button. 70% of the titlebar height
     * is a decent size. (Equals 21 px by default)
     */
    button_width(titlebar_size * BUTTON_HEIGHT_PC),
    button_height(titlebar_size * BUTTON_HEIGHT_PC),
    button_padding((titlebar_size - button_height) / 2),
    theme(th),
    damage_callback(callback)
{}

wf::geometry_t decoration_layout_t::create_buttons(int width, int)
{
    std::stringstream stream((std::string)button_order);
    std::vector<button_type_t> buttons;
    std::string button_name;
    while (stream >> button_name)
    {
        if ((button_name == "minimize") && (theme.button_flags & BUTTON_MINIMIZE))
        {
            buttons.push_back(BUTTON_MINIMIZE);
        }

        if ((button_name == "maximize") && (theme.button_flags & BUTTON_TOGGLE_MAXIMIZE))
        {
            buttons.push_back(BUTTON_TOGGLE_MAXIMIZE);
        }

        if ((button_name == "close") && (theme.button_flags & BUTTON_CLOSE))
        {
            buttons.push_back(BUTTON_CLOSE);
        }
    }

    int per_button = 2 * button_padding + button_width;
    wf::geometry_t button_geometry = {
        width - border_size + button_padding, /* 1 more padding initially */
        button_padding + border_size,
        button_width,
        button_height,
    };

    for (auto type : wf::reverse(buttons))
    {
        button_geometry.x -= per_button;
        this->layout_areas.push_back(std::make_unique<decoration_area_t>(
            button_geometry, damage_callback, theme));
        this->layout_areas.back()->as_button().set_button_type(type);
    }

    int total_width = -button_padding + buttons.size() * per_button;

    return {
        button_geometry.x, border_size,
        total_width, titlebar_size
    };
}

/** Regenerate layout using the new size */
void decoration_layout_t::resize(int width, int height)
{
    this->layout_areas.clear();
    if (this->titlebar_size > 0)
    {
        auto button_geometry_expanded = create_buttons(width, height);

        /* Padding around the button, allows move */
        this->layout_areas.push_back(std::make_unique<decoration_area_t>(
            DECORATION_AREA_MOVE, button_geometry_expanded));

        /* Titlebar dragging area (for move) */
        wf::geometry_t title_geometry = {
            border_size,
            border_size,
            /* Up to the button, but subtract the padding to the left of the
             * title and the padding between title and button */
            button_geometry_expanded.x - border_size,
            titlebar_size,
        };
        this->layout_areas.push_back(std::make_unique<decoration_area_t>(
            DECORATION_AREA_TITLE, title_geometry));
    }

    /* Resizing edges - left */
    wf::geometry_t border_geometry = {0, 0, border_size, height};
    this->layout_areas.push_back(std::make_unique<decoration_area_t>(
        DECORATION_AREA_RESIZE_LEFT, border_geometry));

    /* Resizing edges - right */
    border_geometry = {width - border_size, 0, border_size, height};
    this->layout_areas.push_back(std::make_unique<decoration_area_t>(
        DECORATION_AREA_RESIZE_RIGHT, border_geometry));

    /* Resizing edges - top */
    border_geometry = {0, 0, width, border_size};
    this->layout_areas.push_back(std::make_unique<decoration_area_t>(
        DECORATION_AREA_RESIZE_TOP, border_geometry));

    /* Resizing edges - bottom */
    border_geometry = {0, height - border_size, width, border_size};
    this->layout_areas.push_back(std::make_unique<decoration_area_t>(
        DECORATION_AREA_RESIZE_BOTTOM, border_geometry));
}

/**
 * @return The decoration areas which need to be rendered, in top to bottom
 *  order.
 */
std::vector<nonstd::observer_ptr<decoration_area_t>> decoration_layout_t::get_renderable_areas()
{
    std::vector<nonstd::observer_ptr<decoration_area_t>> renderable;
    for (auto& area : layout_areas)
    {
        if (area->get_type() & DECORATION_AREA_RENDERABLE_BIT)
        {
            renderable.push_back({area});
        }
    }

    return renderable;
}

wf::region_t decoration_layout_t::calculate_region() const
{
    wf::region_t r{};
    for (auto& area : layout_areas)
    {
        auto g = area->get_geometry();
        if ((g.width > 0) && (g.height > 0))
        {
            r |= g;
        }
    }

    return r;
}

void decoration_layout_t::unset_hover(wf::point_t position)
{
    auto area = find_area_at(position);
    if (area && (area->get_type() == DECORATION_AREA_BUTTON))
    {
        area->as_button().set_hover(false);
    }
}

/** Handle motion event to (x, y) relative to the decoration */
decoration_layout_t::action_response_t decoration_layout_t::handle_motion(
    int x, int y)
{
    auto previous_area = find_area_at(current_input);
    auto current_area  = find_area_at({x, y});

    if (previous_area == current_area)
    {
        if (is_grabbed && current_area &&
            (current_area->get_type() & DECORATION_AREA_MOVE_BIT))
        {
            is_grabbed = false;
            return {DECORATION_ACTION_MOVE, 0};
        }
    } else
    {
        unset_hover(current_input);
        if (current_area && (current_area->get_type() == DECORATION_AREA_BUTTON))
        {
            current_area->as_button().set_hover(true);
        }
    }

    this->current_input = {x, y};
    update_cursor();

    return {DECORATION_ACTION_NONE, 0};
}

/**
 * Handle press or release event.
 * @param pressed Whether the event is a press(true) or release(false)
 *  event.
 * @return The action which needs to be carried out in response to this
 *  event.
 * */
decoration_layout_t::action_response_t decoration_layout_t::handle_press_event(
    bool pressed)
{
    if (pressed)
    {
        auto area = find_area_at(current_input);
        if (area && (area->get_type() & DECORATION_AREA_MOVE_BIT))
        {
            if (timer.is_connected())
            {
                double_click_at_release = true;
            } else
            {
                timer.set_timeout(300, [] () { return false; });
            }
        }

        if (area && (area->get_type() & DECORATION_AREA_RESIZE_BIT))
        {
            return {DECORATION_ACTION_RESIZE, calculate_resize_edges()};
        }

        if (area && (area->get_type() == DECORATION_AREA_BUTTON))
        {
            area->as_button().set_pressed(true);
        }

        is_grabbed  = true;
        grab_origin = current_input;
    }

    if (!pressed && double_click_at_release)
    {
        double_click_at_release = false;
        return {DECORATION_ACTION_TOGGLE_MAXIMIZE, 0};
    } else if (!pressed && is_grabbed)
    {
        is_grabbed = false;
        auto begin_area = find_area_at(grab_origin);
        auto end_area   = find_area_at(current_input);

        if (begin_area && (begin_area->get_type() == DECORATION_AREA_BUTTON))
        {
            begin_area->as_button().set_pressed(false);
            if (end_area && (begin_area == end_area))
            {
                switch (begin_area->as_button().get_button_type())
                {
                  case BUTTON_CLOSE:
                    return {DECORATION_ACTION_CLOSE, 0};

                  case BUTTON_TOGGLE_MAXIMIZE:
                    return {DECORATION_ACTION_TOGGLE_MAXIMIZE, 0};

                  case BUTTON_MINIMIZE:
                    return {DECORATION_ACTION_MINIMIZE, 0};

                  default:
                    break;
                }
            }
        }
    }

    return {DECORATION_ACTION_NONE, 0};
}

/**
 * Find the layout area at the given coordinates, if any
 * @return The layout area or null on failure
 */
nonstd::observer_ptr<decoration_area_t> decoration_layout_t::find_area_at(
    wf::point_t point)
{
    for (auto& area : this->layout_areas)
    {
        if (area->get_geometry() & point)
        {
            return {area};
        }
    }

    return nullptr;
}

/** Calculate resize edges based on @current_input */
uint32_t decoration_layout_t::calculate_resize_edges() const
{
    uint32_t edges = 0;
    for (auto& area : layout_areas)
    {
        if (area->get_geometry() & this->current_input)
        {
            if (area->get_type() & DECORATION_AREA_RESIZE_BIT)
            {
                edges |= (area->get_type() & ~DECORATION_AREA_RESIZE_BIT);
            }
        }
    }

    return edges;
}

/** Update the cursor based on @current_input */
void decoration_layout_t::update_cursor() const
{
    uint32_t edges   = calculate_resize_edges();
    auto cursor_name = edges > 0 ?
        wlr_xcursor_get_resize_name((wlr_edges)edges) : "default";
    wf::get_core().set_cursor(cursor_name);
}

void decoration_layout_t::handle_focus_lost()
{
    if (is_grabbed)
    {
        this->is_grabbed = false;
        auto area = find_area_at(grab_origin);
        if (area && (area->get_type() == DECORATION_AREA_BUTTON))
        {
            area->as_button().set_pressed(false);
        }
    }

    this->unset_hover(current_input);
}
}
}
