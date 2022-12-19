#pragma once

#include <wayfire/nonstd/wlroots-full.hpp>
#include "../view/surface-impl.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/scene.hpp"

namespace wf
{
struct drag_icon_t : public wlr_child_surface_base_t
{
    wlr_drag_icon *icon;
    wl_listener_wrapper on_map, on_unmap, on_destroy;

    drag_icon_t(wlr_drag_icon *icon);
    ~drag_icon_t();

    /** Called each time the DnD icon position changes. */
    void update_position();

    /* Force map without receiving a wlroots event */
    void force_map()
    {
        this->map(icon->surface);
    }

    /** Last icon box. */
    wf::geometry_t last_box = {0, 0, 0, 0};
    wf::point_t get_position();

    wf::point_t get_offset() override
    {
        return {0, 0};
    }
};
}
