#pragma once

#include <wayfire/nonstd/wlroots-full.hpp>
#include "wayfire/geometry.hpp"
#include "wayfire/scene.hpp"

namespace wf
{
struct drag_icon_t
{
    wlr_drag_icon *icon;
    wl_listener_wrapper on_map, on_unmap, on_destroy;

    drag_icon_t(wlr_drag_icon *icon);
    ~drag_icon_t();

    /** Called each time the DnD icon position changes. */
    void update_position();

    /** Last icon box. */
    wf::geometry_t last_box = {0, 0, 0, 0};
    wf::point_t get_position();

    scene::floating_inner_ptr root_node;
};
}
