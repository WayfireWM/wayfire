#pragma once

#include <vector>
#include <wayfire/region.hpp>
#include "deco-button.hpp"

namespace wf
{
namespace decor
{
static constexpr uint32_t DECORATION_AREA_RENDERABLE_BIT = (1 << 16);
static constexpr uint32_t DECORATION_AREA_RESIZE_BIT     = (1 << 17);
static constexpr uint32_t DECORATION_AREA_MOVE_BIT = (1 << 18);


/** Different types of areas around the decoration */
enum decoration_area_type_t
{
    DECORATION_AREA_MOVE          = DECORATION_AREA_MOVE_BIT,
    DECORATION_AREA_TITLE         =
        DECORATION_AREA_MOVE_BIT | DECORATION_AREA_RENDERABLE_BIT,
    DECORATION_AREA_BUTTON        = DECORATION_AREA_RENDERABLE_BIT,
    DECORATION_AREA_RESIZE_LEFT   = WLR_EDGE_LEFT | DECORATION_AREA_RESIZE_BIT,
    DECORATION_AREA_RESIZE_RIGHT  = WLR_EDGE_RIGHT | DECORATION_AREA_RESIZE_BIT,
    DECORATION_AREA_RESIZE_TOP    = WLR_EDGE_TOP | DECORATION_AREA_RESIZE_BIT,
    DECORATION_AREA_RESIZE_BOTTOM = WLR_EDGE_BOTTOM | DECORATION_AREA_RESIZE_BIT,
};

/**
 * Represents an area of the decoration which reacts to input events.
 */
struct decoration_area_t
{
  public:
    /**
     * Initialize a new decoration area with the given type and geometry
     */
    decoration_area_t(decoration_area_type_t type, wf::geometry_t g);

    /**
     * Initialize a new decoration area holding a button.
     *
     * @param g The geometry of the button.
     * @param damage_callback Callback to execute when button needs repaint.
     * @param theme The theme to use for the button.
     */
    decoration_area_t(wf::geometry_t g,
        std::function<void(wlr_box)> damage_callback,
        const decoration_theme_t& theme);

    /** @return The geometry of the decoration area, relative to the layout */
    wf::geometry_t get_geometry() const;

    /** @return The area's button, if the area is a button. Otherwise UB */
    button_t& as_button();

    /** @return The type of the decoration area */
    decoration_area_type_t get_type() const;

  private:
    decoration_area_type_t type;
    wf::geometry_t geometry;

    /* For buttons only */
    std::unique_ptr<button_t> button;
};


/**
 * Action which needs to be taken in response to an input event
 */
enum decoration_layout_action_t
{
    DECORATION_ACTION_NONE            = 0,
    /* Drag actions */
    DECORATION_ACTION_MOVE            = 1,
    DECORATION_ACTION_RESIZE          = 2,
    /* Button actions */
    DECORATION_ACTION_CLOSE           = 3,
    DECORATION_ACTION_TOGGLE_MAXIMIZE = 4,
    DECORATION_ACTION_MINIMIZE        = 5,
};

class decoration_theme_t;
/**
 * Manages the layout of the decorations, i.e positioning of the title,
 * buttons, etc.
 *
 * Also dispatches the input events to the appropriate place.
 */
class decoration_layout_t
{
  public:
    /**
     * Create a new decoration layout for the given theme.
     * When the theme changes, the decoration layout needs to be created again.
     *
     * @param damage_callback The function to be called when a part of the
     * layout needs a repaint.
     */
    decoration_layout_t(const decoration_theme_t& theme,
        std::function<void(wlr_box)> damage_callback);

    /** Regenerate layout using the new size */
    void resize(int width, int height);

    /**
     * @return The decoration areas which need to be rendered, in top to bottom
     *  order.
     */
    std::vector<nonstd::observer_ptr<decoration_area_t>> get_renderable_areas();

    /** @return The combined region of all layout areas */
    wf::region_t calculate_region() const;

    struct action_response_t
    {
        decoration_layout_action_t action;
        /* For resizing action, determine the edges for resize request */
        uint32_t edges;
    };

    /** Handle motion event to (x, y) relative to the decoration */
    action_response_t handle_motion(int x, int y);

    /**
     * Handle press or release event.
     * @param pressed Whether the event is a press(true) or release(false)
     *  event.
     * @return The action which needs to be carried out in response to this
     *  event.
     */
    action_response_t handle_press_event(bool pressed = true);

    /**
     * Handle focus lost event.
     */
    void handle_focus_lost();

  private:
    const int titlebar_size;
    const int border_size;
    const int button_width;
    const int button_height;
    const int button_padding;
    const decoration_theme_t& theme;

    std::function<void(wlr_box)> damage_callback;

    std::vector<std::unique_ptr<decoration_area_t>> layout_areas;

    bool is_grabbed = false;
    /* Position where the grab has started */
    wf::point_t grab_origin;
    /* Last position of the input */
    wf::point_t current_input;
    /* double-click timer */
    wf::wl_timer<false> timer;
    bool double_click_at_release = false;

    /** Create buttons in the layout, and return their total geometry */
    wf::geometry_t create_buttons(int width, int height);

    /** Calculate resize edges based on @current_input */
    uint32_t calculate_resize_edges() const;
    /** Update the cursor based on @current_input */
    void update_cursor() const;

    /**
     * Find the layout area at the given coordinates, if any
     * @return The layout area or null on failure
     */
    nonstd::observer_ptr<decoration_area_t> find_area_at(wf::point_t point);

    /** Unset hover state of hovered button at @position, if any */
    void unset_hover(wf::point_t position);
    wf::option_wrapper_t<std::string> button_order{"decoration/button_order"};
};
}
}
