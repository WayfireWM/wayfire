#pragma once

#include "wayfire/workspace-set.hpp" // IWYU pragma: keep
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <map>
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/output.hpp"

namespace wf
{
/**
 * When the workspace wall is rendered via a render hook, the frame event
 * is emitted on each frame.
 *
 * The target framebuffer is passed as signal data.
 */
struct wall_frame_event_t
{
    const wf::render_target_t& target;
    wall_frame_event_t(const wf::render_target_t& t) : target(t)
    {}
};

/**
 * A helper class to render workspaces arranged in a grid.
 */
class workspace_wall_t : public wf::signal::provider_t
{
  public:
    /**
     * Create a new workspace wall on the given output.
     */
    workspace_wall_t(wf::output_t *_output);

    ~workspace_wall_t();

    /**
     * Set the color of the background outside of workspaces.
     *
     * @param color The new background color.
     */
    void set_background_color(const wf::color_t& color);

    /**
     * Set the size of the gap between adjacent workspaces, both horizontally
     * and vertically.
     *
     * @param size The new gap size, in pixels.
     */
    void set_gap_size(int size);

    /**
     * Set which part of the workspace wall to render.
     *
     * If the output has effective resolution WxH and the gap size is G, then a
     * workspace with coordinates (i, j) has geometry
     * {i * (W + G), j * (H + G), W, H}.
     *
     * All other regions are painted with the background color.
     *
     * @param viewport_geometry The part of the workspace wall to render.
     */
    void set_viewport(const wf::geometry_t& viewport_geometry);

    wf::geometry_t get_viewport() const;

    /**
     * Render the selected viewport on the framebuffer.
     *
     * @param fb The framebuffer to render on.
     * @param geometry The rectangle in fb to draw to, in the same coordinate
     *   system as the framebuffer's geometry.
     */
    void render_wall(const wf::render_target_t& fb, const wf::region_t& damage);

    /**
     * Register a render hook and paint the whole output as a desktop wall
     * with the set parameters.
     */
    void start_output_renderer();

    /**
     * Stop repainting the whole output.
     *
     * @param reset_viewport If true, the viewport will be reset to {0, 0, 0, 0}
     *   and thus all workspace streams will be stopped.
     */
    void stop_output_renderer(bool reset_viewport);

    /**
     * Calculate the geometry of a particular workspace, as described in
     * set_viewport().
     *
     * @param ws The workspace whose geometry is to be computed.
     */
    wf::geometry_t get_workspace_rectangle(const wf::point_t& ws) const;

    /**
     * Calculate the whole workspace wall region, including gaps around it.
     */
    wf::geometry_t get_wall_rectangle() const;

    /**
     * Get/set the dimming factor for a given workspace.
     */
    void set_ws_dim(const wf::point_t& ws, float value);

  protected:
    wf::output_t *output;

    wf::color_t background_color = {0, 0, 0, 0};
    int gap_size = 0;
    wf::geometry_t viewport = {0, 0, 0, 0};
    std::map<std::pair<int, int>, float> render_colors;
    float get_color_for_workspace(wf::point_t ws);

    /**
     * Get a list of workspaces visible in the viewport.
     */
    std::vector<wf::point_t> get_visible_workspaces(wf::geometry_t viewport) const;

  protected:
    class workspace_wall_node_t;
    std::shared_ptr<workspace_wall_node_t> render_node;
};
}
