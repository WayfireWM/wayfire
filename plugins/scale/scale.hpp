#pragma once

#include "wayfire/debug.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/plugins/common/util.hpp"
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>

inline wayfire_toplevel_view scale_find_view_at(wf::pointf_t at, wf::output_t *output)
{
    auto offset = wf::origin(output->get_layout_geometry());
    at.x -= offset.x;
    at.y -= offset.y;
    return wf::find_output_view_at(output, at);
}
