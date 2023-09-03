#pragma once

#include "wayfire/toplevel-view.hpp"
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>

inline wayfire_toplevel_view scale_find_view_at(wf::pointf_t at, wf::output_t *output)
{
    auto offset = wf::origin(output->get_layout_geometry());
    at.x -= offset.x;
    at.y -= offset.y;

    auto node = output->wset()->get_node()->find_node_at(at);
    if (node)
    {
        return wf::toplevel_cast(wf::node_to_view(node->node->shared_from_this()));
    }

    return nullptr;
}
