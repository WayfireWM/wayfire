#pragma once
#include <wayfire/nonstd/wlroots-full.hpp>

namespace wf
{
/**
 * A signal emitted whenever a new xdg_surface object was created on the wlroots side.
 * By using this signal, plugins may indicate to core that they want to override the view implementation for
 * the given surface.
 */
struct new_xdg_surface_signal
{
    wlr_xdg_surface *surface;

    /**
     * If a plugin sets this to false, then that plugin is responsible for allocating a view and the
     * corresponding nodes for the xdg_surface. Core will not handle the xdg_surface any further.
     */
    bool use_default_implementation = true;
};
}
