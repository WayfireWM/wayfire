#pragma once

#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include <wayfire/opengl.hpp>
#include <wayfire/object.hpp>
#include <wayfire/region.hpp>

namespace wf
{
/**
 * A workspace stream is a special node which displays a workspace of an output.
 */
class workspace_stream_node_t : public scene::node_t
{
  public:
    workspace_stream_node_t(wf::output_t *output, wf::point_t workspace);

    // The color of the background of the workspace stream.
    // If not set, the default background color (specified in the config file)
    // of Wayfire is used.
    std::optional<wf::color_t> background;

    wf::output_t*const output;
    const wf::point_t ws;

    // node_t implementation

  public:
    std::string stringify() const override;
    void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
        scene::damage_callback push_damage, wf::output_t *output) override;
    // The bounding box of a workspace stream is
    // (0, 0, output_width, output_height).
    wf::geometry_t get_bounding_box() override;
    class workspace_stream_instance_t;

  private:
};
}
