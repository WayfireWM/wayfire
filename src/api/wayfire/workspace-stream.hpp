#pragma once

#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include <wayfire/opengl.hpp>
#include <wayfire/object.hpp>
#include <wayfire/region.hpp>

namespace wf
{
/** A workspace stream is a way for plugins to obtain the contents of a
 * given workspace.  */
class workspace_stream_t
{
  public:
    std::vector<scene::render_instance_uptr> instances;
    wf::region_t accumulated_damage;
    signal::connection_t<scene::root_node_update_signal> regen_instances;

    // not-null => is running
    wf::output_t *current_output = NULL;

    wf::point_t ws;
    wf::framebuffer_t buffer;

    /* The background color of the stream, when there is no view above it.
     * All streams start with -1.0 alpha to indicate that the color is
     * invalid. In this case, we use the default color, which can
     * optionally be set by the user. If a plugin changes the background,
     * the color will be valid and it will be used instead. This way,
     * plugins can choose the background color they want first and if
     * it is not set (alpha = -1.0) it will fallback to the default
     * user configurable color. */
    wf::color_t background = {0.0f, 0.0f, 0.0f, -1.0f};

    /**
     * Start the workspace stream, that is, initialize the stream instances.
     * Note that the user of this API should set @buffer before starting.
     */
    void start_for_workspace(wf::output_t *output, wf::point_t workspace);

    /**
     * Update the contents of the workspace stream.
     */
    void render_frame();

    /**
     * Stop the workspace stream and free up the instances.
     */
    void stop();

  private:
    void update_instances();
};
}
