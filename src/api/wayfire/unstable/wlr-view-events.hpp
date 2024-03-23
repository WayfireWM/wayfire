#pragma once

// WF_USE_CONFIG_H is set only when building Wayfire itself, external plugins
// need to use <wayfire/config.h>
#ifdef WF_USE_CONFIG_H
    #include <config.h>
#else
    #include <wayfire/config.h>
#endif

#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/view.hpp>

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

#if WF_HAS_XWAYLAND
/**
 * A signal emitted whenever a new wlr_xwayland_surface object was created on the wlroots side.
 * By using this signal, plugins may indicate to core that they want to override the view implementation for
 * the given surface.
 */
struct new_xwayland_surface_signal
{
    wlr_xwayland_surface *surface;

    /**
     * If a plugin sets this to false, then that plugin is responsible for allocating a view and the
     * corresponding nodes for the xwayland_surface. Core will not handle the xwayland_surface any further.
     */
    bool use_default_implementation = true;
};

#endif

/**
 * A signal emitted on core when a view with the default wayfire implementation is about to be mapped.
 * Plugins can take a look at the view and decide to overwrite its implementation.
 */
struct view_pre_map_signal
{
    /**
     * The view which will be mapped after this signal, if plugins do not override it.
     */
    wf::view_interface_t *view;

    /**
     * The wlr-surface of the view.
     */
    wlr_surface *surface;

    /**
     * Plugins can set this to override the view implementation. If they do so, the view will not be mapped,
     * and instead the default controller and view implementation for the view will be destroyed after the
     * signal. Plugins are then free to provide a view implementation themselves.
     */
    bool override_implementation = false;
};
}
