#pragma once


#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/region.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene.hpp"
#include "workspace-stream-sharing.hpp"

namespace wf
{
/**
 * When the workspace wall is rendered via a render hook, the frame event
 * is emitted on each frame.
 *
 * The target framebuffer is passed as signal data.
 */
struct wall_frame_event_t : public signal_data_t
{
    const wf::render_target_t& target;
    wall_frame_event_t(const wf::render_target_t& t) : target(t)
    {}
};

/**
 * A helper class to render workspaces arranged in a grid.
 */
class workspace_wall_t : public wf::signal_provider_t
{
  public:
    /**
     * Create a new workspace wall on the given output.
     */
    workspace_wall_t(wf::output_t *_output) : output(_output)
    {
        this->viewport = get_wall_rectangle();
        streams = workspace_stream_pool_t::ensure_pool(output);

        resize_colors();
        output->connect_signal("workspace-grid-changed", &on_workspace_grid_changed);
    }

    ~workspace_wall_t()
    {
        stop_output_renderer(false);
        streams->unref();
    }

    /**
     * Set the color of the background outside of workspaces.
     *
     * @param color The new background color.
     */
    void set_background_color(const wf::color_t& color)
    {
        this->background_color = color;
    }

    /**
     * Set the size of the gap between adjacent workspaces, both horizontally
     * and vertically.
     *
     * @param size The new gap size, in pixels.
     */
    void set_gap_size(int size)
    {
        this->gap_size = size;
    }

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
    void set_viewport(const wf::geometry_t& viewport_geometry)
    {
        /*
         * XXX: Check which workspaces should be stopped.
         * Algorithm can be reduced to O(N) but O(N^2) should be more than enough.
         */
        auto previously_visible = get_visible_workspaces(this->viewport);
        auto newly_visible = get_visible_workspaces(viewport_geometry);
        for (wf::point_t old : previously_visible)
        {
            auto it = std::find_if(newly_visible.begin(), newly_visible.end(),
                [&] (auto neww) { return neww == old; });
            if (it == newly_visible.end())
            {
                streams->stop(old);
            }
        }

        this->viewport = viewport_geometry;
    }

    /**
     * Render the selected viewport on the framebuffer.
     *
     * @param fb The framebuffer to render on.
     * @param geometry The rectangle in fb to draw to, in the same coordinate
     *   system as the framebuffer's geometry.
     */
    void render_wall(const wf::render_target_t& fb, const wf::region_t& damage)
    {
        update_streams();

        auto wall_matrix = calculate_viewport_transformation_matrix(this->viewport,
            output->get_relative_geometry());

        OpenGL::render_begin(fb);
        for (auto box : damage)
        {
            fb.logic_scissor(wlr_box_from_pixman_box(box));

            OpenGL::clear(this->background_color);
            /* After all transformations of the framebuffer, the workspace should
             * span the visible part of the OpenGL coordinate space. */
            const wf::geometry_t workspace_geometry = {-1, 1, 2, -2};
            for (auto& ws : get_visible_workspaces(this->viewport))
            {
                auto ws_matrix = calculate_workspace_matrix(ws);
                OpenGL::render_transformed_texture(
                    streams->get(ws).buffer.tex, workspace_geometry,
                    fb.get_orthographic_projection() * wall_matrix * ws_matrix,
                    get_ws_color(ws));
            }

            OpenGL::render_end();
        }

        wall_frame_event_t data{fb};
        this->emit_signal("frame", &data);
    }

    /**
     * Register a render hook and paint the whole output as a desktop wall
     * with the set parameters.
     */
    void start_output_renderer()
    {
        wf::dassert(render_node == nullptr, "Starting workspace-wall twice?");
        render_node = std::make_shared<workspace_wall_node_t>(this);
        scene::add_front(wf::get_core().scene(), render_node);
    }

    /**
     * Stop repainting the whole output.
     *
     * @param reset_viewport If true, the viewport will be reset to {0, 0, 0, 0}
     *   and thus all workspace streams will be stopped.
     */
    void stop_output_renderer(bool reset_viewport)
    {
        wf::dassert(render_node != nullptr,
            "Stopping without having started workspace-wall?");

        scene::remove_child(render_node);
        render_node = nullptr;

        if (reset_viewport)
        {
            set_viewport({0, 0, 0, 0});
        }
    }

    /**
     * Calculate the geometry of a particular workspace, as described in
     * set_viewport().
     *
     * @param ws The workspace whose geometry is to be computed.
     */
    wf::geometry_t get_workspace_rectangle(const wf::point_t& ws) const
    {
        auto size = this->output->get_screen_size();

        return {
            ws.x * (size.width + gap_size),
            ws.y * (size.height + gap_size),
            size.width,
            size.height
        };
    }

    /**
     * Calculate the whole workspace wall region, including gaps around it.
     */
    wf::geometry_t get_wall_rectangle() const
    {
        auto size = this->output->get_screen_size();
        auto workspace_size = this->output->workspace->get_workspace_grid_size();

        return {
            -gap_size,
            -gap_size,
            workspace_size.width * (size.width + gap_size) + gap_size,
            workspace_size.height * (size.height + gap_size) + gap_size
        };
    }

    /**
     * Get the color multiplier for a given workspace.
     * This can be used to set to a desired color as well.
     */
    glm::vec4& get_ws_color(const wf::point_t& ws)
    {
        return render_colors.at(ws.x).at(ws.y);
    }

  protected:
    wf::output_t *output;

    wf::color_t background_color = {0, 0, 0, 0};
    int gap_size = 0;

    wf::geometry_t viewport = {0, 0, 0, 0};
    nonstd::observer_ptr<workspace_stream_pool_t> streams;

    std::vector<std::vector<glm::vec4>> render_colors;

    /** Update or start visible streams */
    void update_streams()
    {
        for (auto& ws : get_visible_workspaces(viewport))
        {
            streams->update(ws);
        }
    }

    /**
     * Get a list of workspaces visible in the viewport.
     */
    std::vector<wf::point_t> get_visible_workspaces(wf::geometry_t viewport) const
    {
        std::vector<wf::point_t> visible;
        auto wsize = output->workspace->get_workspace_grid_size();
        for (int i = 0; i < wsize.width; i++)
        {
            for (int j = 0; j < wsize.height; j++)
            {
                if (viewport & get_workspace_rectangle({i, j}))
                {
                    visible.push_back({i, j});
                }
            }
        }

        return visible;
    }

    /**
     * Calculate the workspace matrix.
     *
     * Workspaces are always rendered with width/height 2 and centered around (0, 0).
     * To obtain the correct output image, the following is done:
     *
     * 1. Output rotation is undone from the workspace stream texture.
     * 2. Workspace quad is scaled to the correct size.
     * 3. Workspace quad is translated to the correct global position.
     */
    glm::mat4 calculate_workspace_matrix(const wf::point_t& ws) const
    {
        auto target_geometry = get_workspace_rectangle(ws);
        auto fb = output->render->get_target_framebuffer();
        auto translation = glm::translate(glm::mat4(1.0),
            glm::vec3{target_geometry.x, target_geometry.y, 0.0});

        return translation * glm::inverse(fb.get_orthographic_projection());
    }

    /**
     * Calculate the viewport transformation matrix.
     *
     * This matrix transforms the workspace's quad from the logical wall space
     * to the actual box to be displayed on the screen.
     */
    glm::mat4 calculate_viewport_transformation_matrix(
        const wf::geometry_t& viewport, const wf::geometry_t& target) const
    {
        const double scale_x = target.width * 1.0 / viewport.width;
        const double scale_y = target.height * 1.0 / viewport.height;

        const double x_after_scale = viewport.x * scale_x;
        const double y_after_scale = viewport.y * scale_y;

        auto scaling = glm::scale(glm::mat4(
            1.0), glm::vec3{scale_x, scale_y, 1.0});
        auto translation = glm::translate(glm::mat4(1.0),
            glm::vec3{target.x - x_after_scale, target.y - y_after_scale, 0.0});

        return translation * scaling;
    }

    void resize_colors()
    {
        auto size = this->output->workspace->get_workspace_grid_size();
        render_colors.resize(size.width);
        for (auto& v : render_colors)
        {
            v.resize(size.height, glm::vec4(1.f));
        }
    }

    wf::signal_connection_t on_workspace_grid_changed = [=] (auto)
    {
        resize_colors();
    };

  protected:
    class workspace_wall_node_t : public scene::node_t
    {
        class wwall_render_instance_t : public scene::render_instance_t
        {
            workspace_wall_node_t *self;

          public:
            wwall_render_instance_t(workspace_wall_node_t *self)
            {
                this->self = self;
            }

            void schedule_instructions(
                std::vector<scene::render_instruction_t>& instructions,
                const wf::render_target_t& target, wf::region_t& damage) override
            {
                auto bbox = self->get_bounding_box();
                instructions.push_back(scene::render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & bbox,
                    });

                damage ^= bbox;
            }

            void render(const wf::render_target_t& target,
                const wf::region_t& region) override
            {
                self->wall->render_wall(target, region);
            }
        };

      public:
        workspace_wall_node_t(workspace_wall_t *wall) : node_t(false)
        {
            this->wall = wall;
        }

        virtual void gen_render_instances(
            std::vector<scene::render_instance_uptr>& instances,
            scene::damage_callback push_damage, wf::output_t *shown_on)
        {
            if (shown_on != this->wall->output)
            {
                return;
            }

            instances.push_back(std::make_unique<wwall_render_instance_t>(this));
        }

        wf::geometry_t get_bounding_box()
        {
            return wall->output->get_layout_geometry();
        }

      private:
        workspace_wall_t *wall;
    };
    std::shared_ptr<workspace_wall_node_t> render_node;
};
}
