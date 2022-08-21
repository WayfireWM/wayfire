#pragma once

#include <wayfire/opengl.hpp>
#include <wayfire/object.hpp>
#include <wayfire/region.hpp>

namespace wf
{
/** A workspace stream is a way for plugins to obtain the contents of a
 * given workspace.  */
struct workspace_stream_t
{
    wf::point_t ws;
    wf::framebuffer_t buffer;
    bool running = false;

    float scale_x = 1.0;
    float scale_y = 1.0;

    /* The background color of the stream, when there is no view above it.
     * All streams start with -1.0 alpha to indicate that the color is
     * invalid. In this case, we use the default color, which can
     * optionally be set by the user. If a plugin changes the background,
     * the color will be valid and it will be used instead. This way,
     * plugins can choose the background color they want first and if
     * it is not set (alpha = -1.0) it will fallback to the default
     * user configurable color. */
    wf::color_t background = {0.0f, 0.0f, 0.0f, -1.0f};
};

/**
 * name: workspace-stream-pre, workspace-stream-post
 * on: render-manager
 * when: Immediately before(after) repainting a workspace stream.
 */
struct stream_signal_t : public wf::signal_data_t
{
    stream_signal_t(wf::point_t _ws, wf::region_t& damage,
        const wf::render_target_t& _fb) :
        ws(_ws), raw_damage(damage), fb(_fb)
    {}

    /** The coordinates of the workspace this workspace stream is for. */
    wf::point_t ws;
    /** The damage on the stream, in output-local coordinates */
    wf::region_t& raw_damage;
    /** The framebuffer of the stream, fb has output-local geometry. */
    const wf::render_target_t& fb;
};
}
