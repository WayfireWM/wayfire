#pragma once


#include <any>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/workspace-stream.hpp"
#include "wayfire/workspace-set.hpp"

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
    workspace_wall_t(wf::output_t *_output) : output(_output)
    {
        this->viewport = get_wall_rectangle();
    }

    ~workspace_wall_t()
    {
        stop_output_renderer(false);
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
        if (viewport_geometry == viewport)
        {
            return;
        }

        this->viewport = viewport_geometry;
        if (render_node)
        {
            scene::damage_node(this->render_node,
                this->render_node->get_bounding_box());
        }
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
        wall_frame_event_t data{fb};
        this->emit(&data);
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
        if (!render_node)
        {
            return;
        }

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
        auto workspace_size = this->output->wset()->get_workspace_grid_size();

        return {
            -gap_size,
            -gap_size,
            workspace_size.width * (size.width + gap_size) + gap_size,
            workspace_size.height * (size.height + gap_size) + gap_size
        };
    }

    /**
     * Get/set the dimming factor for a given workspace.
     */
    void set_ws_dim(const wf::point_t& ws, float value)
    {
        render_colors[{ws.x, ws.y}] = value;
        if (render_node)
        {
            scene::damage_node(render_node, render_node->get_bounding_box());
        }
    }

  protected:
    wf::output_t *output;

    wf::color_t background_color = {0, 0, 0, 0};
    int gap_size = 0;
    wf::geometry_t viewport = {0, 0, 0, 0};

    std::map<std::pair<int, int>, float> render_colors;

    float get_color_for_workspace(wf::point_t ws)
    {
        auto it = render_colors.find({ws.x, ws.y});
        if (it == render_colors.end())
        {
            return 1.0;
        }

        return it->second;
    }

    /**
     * Get a list of workspaces visible in the viewport.
     */
    std::vector<wf::point_t> get_visible_workspaces(wf::geometry_t viewport) const
    {
        std::vector<wf::point_t> visible;
        auto wsize = output->wset()->get_workspace_grid_size();
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

  protected:
    class workspace_wall_node_t : public scene::node_t
    {
        class wwall_render_instance_t : public scene::render_instance_t
        {
            workspace_wall_node_t *self;

            std::vector<std::vector<std::vector<scene::render_instance_uptr>>>
            instances;

            scene::damage_callback push_damage;
            wf::signal::connection_t<scene::node_damage_signal> on_wall_damage =
                [=] (scene::node_damage_signal *ev)
            {
                push_damage(ev->region);
            };

            wf::geometry_t get_workspace_rect(wf::point_t ws)
            {
                auto output_size = self->wall->output->get_screen_size();
                return {
                    .x     = ws.x * (output_size.width + self->wall->gap_size),
                    .y     = ws.y * (output_size.height + self->wall->gap_size),
                    .width = output_size.width,
                    .height = output_size.height,
                };
            }

          public:
            wwall_render_instance_t(workspace_wall_node_t *self,
                scene::damage_callback push_damage)
            {
                this->self = self;
                this->push_damage = push_damage;
                self->connect(&on_wall_damage);

                instances.resize(self->workspaces.size());
                for (int i = 0; i < (int)self->workspaces.size(); i++)
                {
                    instances[i].resize(self->workspaces[i].size());
                    for (int j = 0; j < (int)self->workspaces[i].size(); j++)
                    {
                        auto push_damage_child = [=] (const wf::region_t& damage)
                        {
                            wf::region_t our_damage;
                            for (auto& rect : damage)
                            {
                                wf::geometry_t box = wlr_box_from_pixman_box(rect);
                                box = box + wf::origin(get_workspace_rect({i, j}));
                                auto A = self->wall->viewport;
                                auto B = self->get_bounding_box();
                                our_damage |= scale_box(A, B, box);
                            }

                            push_damage(our_damage);
                        };

                        self->workspaces[i][j]->gen_render_instances(instances[i][j],
                            push_damage_child, self->wall->output);
                    }
                }
            }

            using render_tag = std::tuple<int, float>;
            static constexpr int TAG_BACKGROUND = 0;
            static constexpr int TAG_WS_DIM     = 1;
            static constexpr int FRAME_EV = 2;

            void schedule_instructions(
                std::vector<scene::render_instruction_t>& instructions,
                const wf::render_target_t& target, wf::region_t& damage) override
            {
                instructions.push_back(scene::render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = wf::region_t{},
                        .data     = render_tag{FRAME_EV, 0.0},
                    });

                // Scale damage to be in the workspace's coordinate system
                wf::region_t workspaces_damage;
                for (auto& rect : damage)
                {
                    auto box = wlr_box_from_pixman_box(rect);
                    wf::geometry_t A = self->get_bounding_box();
                    wf::geometry_t B = self->wall->viewport;
                    workspaces_damage |= scale_box(A, B, box);
                }

                for (int i = 0; i < (int)self->workspaces.size(); i++)
                {
                    for (int j = 0; j < (int)self->workspaces[i].size(); j++)
                    {
                        // Compute render target: a subbuffer of the target buffer
                        // which corresponds to the region occupied by the
                        // workspace.
                        wf::render_target_t our_target = target;
                        our_target.geometry =
                            self->workspaces[i][j]->get_bounding_box();

                        wf::geometry_t workspace_rect = get_workspace_rect({i, j});
                        wf::geometry_t relative_to_viewport = scale_box(
                            self->wall->viewport, target.geometry, workspace_rect);

                        our_target.subbuffer = target.framebuffer_box_from_geometry_box(relative_to_viewport);

                        // Take the damage for the workspace in workspace-local coordindates, as the workspace
                        // stream node expects.
                        wf::region_t our_damage = workspaces_damage & workspace_rect;
                        workspaces_damage ^= our_damage;
                        our_damage += -wf::origin(workspace_rect);

                        // Dim workspaces at the end (the first instruction pushed is executed last)
                        instructions.push_back(scene::render_instruction_t{
                                .instance = this,
                                .target   = our_target,
                                .damage   = our_damage,
                                .data     = render_tag{TAG_WS_DIM,
                                    self->wall->get_color_for_workspace({i, j})},
                            });

                        // Render the workspace contents first
                        for (auto& ch : instances[i][j])
                        {
                            ch->schedule_instructions(instructions, our_target, our_damage);
                        }
                    }
                }

                auto bbox = self->get_bounding_box();

                instructions.push_back(scene::render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & self->get_bounding_box(),
                        .data     = render_tag{TAG_BACKGROUND, 0.0},
                    });

                damage ^= bbox;
            }

            void render(const wf::render_target_t& target,
                const wf::region_t& region, const std::any& any_tag) override
            {
                auto [tag, dim] = std::any_cast<render_tag>(any_tag);

                if (tag == TAG_BACKGROUND)
                {
                    OpenGL::render_begin(target);
                    for (auto& box : region)
                    {
                        target.logic_scissor(wlr_box_from_pixman_box(box));
                        OpenGL::clear(self->wall->background_color);
                    }

                    OpenGL::render_end();
                } else if (tag == FRAME_EV)
                {
                    self->wall->render_wall(target, region);
                } else
                {
                    auto fb_region = target.framebuffer_region_from_geometry_region(region);

                    OpenGL::render_begin(target);
                    for (auto& dmg_rect : fb_region)
                    {
                        target.scissor(wlr_box_from_pixman_box(dmg_rect));
                        const float a = 1.0 - dim;

                        OpenGL::render_rectangle(target.geometry, {0, 0, 0, a},
                            target.get_orthographic_projection());
                    }

                    OpenGL::render_end();
                }
            }

            void compute_visibility(wf::output_t *output, wf::region_t& visible) override
            {
                for (int i = 0; i < (int)self->workspaces.size(); i++)
                {
                    for (int j = 0; j < (int)self->workspaces[i].size(); j++)
                    {
                        wf::region_t ws_region = self->workspaces[i][j]->get_bounding_box();
                        for (auto& ch : this->instances[i][j])
                        {
                            ch->compute_visibility(output, ws_region);
                        }
                    }
                }
            }
        };

      public:
        workspace_wall_node_t(workspace_wall_t *wall) : node_t(false)
        {
            this->wall  = wall;
            auto [w, h] = wall->output->wset()->get_workspace_grid_size();
            workspaces.resize(w);
            for (int i = 0; i < w; i++)
            {
                for (int j = 0; j < h; j++)
                {
                    auto node = std::make_shared<workspace_stream_node_t>(
                        wall->output, wf::point_t{i, j});
                    workspaces[i].push_back(node);
                }
            }
        }

        virtual void gen_render_instances(
            std::vector<scene::render_instance_uptr>& instances,
            scene::damage_callback push_damage, wf::output_t *shown_on) override
        {
            if (shown_on != this->wall->output)
            {
                return;
            }

            instances.push_back(std::make_unique<wwall_render_instance_t>(
                this, push_damage));
        }

        std::string stringify() const override
        {
            return "workspace-wall " + stringify_flags();
        }

        wf::geometry_t get_bounding_box() override
        {
            return wall->output->get_layout_geometry();
        }

      private:
        workspace_wall_t *wall;
        std::vector<std::vector<std::shared_ptr<workspace_stream_node_t>>> workspaces;
    };
    std::shared_ptr<workspace_wall_node_t> render_node;
};
}
