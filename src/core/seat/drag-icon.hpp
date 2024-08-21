#pragma once

#include <wayfire/nonstd/wlroots-full.hpp>
#include "wayfire/geometry.hpp"
#include <wayfire/util.hpp>
#include <memory>

namespace wf
{
namespace scene
{
class dnd_root_icon_root_node_t;
}

class drag_icon_t
{
  public:
    wlr_drag_icon *icon;
    wl_listener_wrapper on_map, on_unmap, on_destroy;

    drag_icon_t(wlr_drag_icon *icon);
    ~drag_icon_t();

    /** Called each time the DnD icon position changes. */
    void update_position();

    /** Last icon box. */
    wf::geometry_t last_box = {0, 0, 0, 0};
    wf::point_t get_position();

    std::shared_ptr<scene::dnd_root_icon_root_node_t> root_node;
};
}
