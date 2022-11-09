// A collection of small utility functions that plugins use.
// FIXME: consider splitting into multiple files as util functions accumulate.
#pragma once

#include "wayfire/view.hpp"
namespace wf
{
inline uint64_t get_focus_timestamp(wayfire_view view)
{
    const auto& node = view->get_surface_root_node();
    return node->keyboard_interaction().last_focus_timestamp;
}
}
