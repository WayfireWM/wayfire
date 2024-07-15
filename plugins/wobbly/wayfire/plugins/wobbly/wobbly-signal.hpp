#pragma once

#include <wayfire/signal-definitions.hpp>
#include <wayfire/core.hpp>
#include <wayfire/toplevel-view.hpp>

enum wobbly_event
{
    WOBBLY_EVENT_GRAB       = (1 << 0),
    WOBBLY_EVENT_MOVE       = (1 << 1),
    WOBBLY_EVENT_END        = (1 << 2),
    WOBBLY_EVENT_ACTIVATE   = (1 << 3),
    WOBBLY_EVENT_TRANSLATE  = (1 << 4),
    WOBBLY_EVENT_FORCE_TILE = (1 << 5),
    WOBBLY_EVENT_UNTILE     = (1 << 6),
    WOBBLY_EVENT_SCALE      = (1 << 7),
};

/**
 * on: core
 * when: This signal is used to control(start/stop/update) the wobbly state
 *   for a view. Note that plugins usually would use the helper functions below,
 *   instead of emitting this signal directly.
 */
struct wobbly_signal
{
    wayfire_toplevel_view view;

    wobbly_event events;

    /**
     * For EVENT_GRAB and EVENT_MOVE: the coordinates of the grab
     * For EVENT_TRANSLATE: the amount of translation
     */
    wf::point_t pos;

    /**
     * For EVENT_SCALE: the new size of the base surface.
     */
    wf::geometry_t geometry;
};

/**
 * Start wobblying when the view is being grabbed, for ex. when moving it
 */
inline void start_wobbly(wayfire_toplevel_view view, int grab_x, int grab_y)
{
    wobbly_signal sig;
    sig.view   = view;
    sig.events = WOBBLY_EVENT_GRAB;
    sig.pos    = {grab_x, grab_y};

    wf::get_core().emit(&sig);
}

/**
 * Start wobblying when the view is being grabbed, for ex. when moving it.
 * The position is relative to the view, i.e [0.5, 0.5] is the midpoint.
 */
inline void start_wobbly_rel(wayfire_toplevel_view view, wf::pointf_t rel_grab)
{
    wobbly_signal sig;
    sig.view   = view;
    sig.events = WOBBLY_EVENT_GRAB;

    auto bbox = view->get_bounding_box();
    sig.pos.x = bbox.x + rel_grab.x * bbox.width;
    sig.pos.y = bbox.y + rel_grab.y * bbox.height;

    wf::get_core().emit(&sig);
}

/**
 * Release the wobbly grab
 */
inline void end_wobbly(wayfire_toplevel_view view)
{
    wobbly_signal sig;
    sig.view   = view;
    sig.events = WOBBLY_EVENT_END;
    wf::get_core().emit(&sig);
}

/**
 * Indicate that the grab has moved (i.e cursor moved, touch moved, etc.)
 */
inline void move_wobbly(wayfire_toplevel_view view, int grab_x, int grab_y)
{
    wobbly_signal sig;
    sig.view   = view;
    sig.events = WOBBLY_EVENT_MOVE;
    sig.pos    = {grab_x, grab_y};
    wf::get_core().emit(&sig);
}

/**
 * Temporarily activate wobbly on the view.
 * This is useful when animating some transition like fullscreening, tiling, etc.
 */
inline void activate_wobbly(wayfire_toplevel_view view)
{
    wobbly_signal sig;
    sig.view   = view;
    sig.events = WOBBLY_EVENT_ACTIVATE;
    wf::get_core().emit(&sig);
}

/**
 * Translate the wobbly model (and its grab point, if any).
 */
inline void translate_wobbly(wayfire_toplevel_view view, wf::point_t delta)
{
    wobbly_signal sig;
    sig.view   = view;
    sig.events = WOBBLY_EVENT_TRANSLATE;
    sig.pos    = delta;
    wf::get_core().emit(&sig);
}

/**
 * Set the wobbly model forcibly (un)tiled.
 * This means that its four corners will be held in place, until the model is
 * untiled.
 */
inline void set_tiled_wobbly(wayfire_toplevel_view view, bool tiled)
{
    wobbly_signal sig;
    sig.view   = view;
    sig.events = tiled ? WOBBLY_EVENT_FORCE_TILE : WOBBLY_EVENT_UNTILE;
    wf::get_core().emit(&sig);
}

/**
 * Change the wobbly model geometry, without re-activating the springs.
 */
inline void modify_wobbly(wayfire_toplevel_view view, wf::geometry_t target)
{
    wobbly_signal sig;
    sig.view     = view;
    sig.events   = WOBBLY_EVENT_SCALE;
    sig.geometry = target;
    wf::get_core().emit(&sig);
}
