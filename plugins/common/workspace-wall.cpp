#include "wayfire/plugins/common/workspace-wall.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/workspace-stream.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/region.hpp"
#include "wayfire/core.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace wf
{
template<class Data> using per_workspace_map_t = std::map<int, std::map<int, Data>>;

class workspace_wall_t::workspace_wall_node_t : public scene::node_t
{
    class wwall_render_instance_t : public scene::render_instance_t
    {
        std::shared_ptr<workspace_wall_node_t> self;
        per_workspace_map_t<std::vector<scene::render_instance_uptr>> instances;

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
            this->self = std::dynamic_pointer_cast<workspace_wall_node_t>(self->shared_from_this());
            this->push_damage = push_damage;
            self->connect(&on_wall_damage);

            for (int i = 0; i < (int)self->workspaces.size(); i++)
            {
                for (int j = 0; j < (int)self->workspaces[i].size(); j++)
                {
                    auto push_damage_child = [=] (const wf::region_t& damage)
                    {
                        // Store the damage because we'll have to update the buffers
                        self->aux_buffer_damage[i][j] |= damage;

                        wf::region_t our_damage;
                        for (auto& rect : damage)
                        {
                            wf::geometry_t box = wlr_box_from_pixman_box(rect);
                            box = box + wf::origin(get_workspace_rect({i, j}));
                            auto A = self->wall->viewport;
                            auto B = self->get_bounding_box();
                            our_damage |= scale_box(A, B, box);
                        }

                        // Also damage the 'screen' after transforming damage
                        push_damage(our_damage);
                    };

                    self->workspaces[i][j]->gen_render_instances(instances[i][j],
                        push_damage_child, self->wall->output);
                }
            }
        }

        static int damage_sum_area(const wf::region_t& damage)
        {
            int sum = 0;
            for (const auto& rect : damage)
            {
                sum += (rect.y2 - rect.y1) * (rect.x2 - rect.x1);
            }

            return sum;
        }

        bool consider_rescale_workspace_buffer(int i, int j, wf::region_t& visible_damage)
        {
            // In general, when rendering the auxilliary buffers for each workspace, we can render the
            // workspace thumbnails in a lower resolution, because at the end they are shown scaled.
            // This helps with performance and uses less GPU power.
            //
            // However, the situation is tricky because during the Expo animation the optimal render
            // scale constantly changes. Thus, in some cases it is actually far from optimal to rescale
            // on every frame - it is often better to just keep the buffers from the old scale.
            //
            // Nonetheless, we need to make sure to rescale when this makes sense, and to avoid visual
            // artifacts.
            auto bbox = self->workspaces[i][j]->get_bounding_box();
            const float render_scale = std::max(
                1.0 * bbox.width / self->wall->viewport.width,
                1.0 * bbox.height / self->wall->viewport.height);
            const float current_scale = self->aux_buffer_current_scale[i][j];

            // Avoid keeping a low resolution if we are going up in the scale (for example, expo exit
            // animation) and we're close to the 1.0 scale. Otherwise, we risk popping artifacts as we
            // suddenly switch from low to high resolution.
            const bool rescale_magnification = (render_scale > 0.5) &&
                (render_scale > current_scale * 1.1);

            // In general, it is worth changing the buffer scale if we have a lot of damage to the old
            // buffer, so that for ex. a full re-scale is actually cheaper than repaiting the old buffer.
            // This could easily happen for example if we have a video player during Expo start animation.
            const int repaint_cost_current_scale =
                damage_sum_area(visible_damage) * (current_scale * current_scale);
            const int repaint_rescale_cost = (bbox.width * bbox.height) * (render_scale * render_scale);

            if ((repaint_cost_current_scale > repaint_rescale_cost) || rescale_magnification)
            {
                self->aux_buffer_current_scale[i][j] = render_scale;
                self->aux_buffers[i][j].subbuffer    = wf::geometry_t{
                    0, 0,
                    int(std::ceil(render_scale * self->aux_buffers[i][j].viewport_width)),
                    int(std::ceil(render_scale * self->aux_buffers[i][j].viewport_height)),
                };

                self->aux_buffer_damage[i][j] |= self->workspaces[i][j]->get_bounding_box();
                return true;
            }

            return false;
        }

        void schedule_instructions(
            std::vector<scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::region_t& damage) override
        {
            // Update workspaces in a render pass
            for (int i = 0; i < (int)self->workspaces.size(); i++)
            {
                for (int j = 0; j < (int)self->workspaces[i].size(); j++)
                {
                    const auto ws_bbox     = self->wall->get_workspace_rectangle({i, j});
                    const auto visible_box =
                        geometry_intersection(self->wall->viewport, ws_bbox) - wf::origin(ws_bbox);
                    wf::region_t visible_damage = self->aux_buffer_damage[i][j] & visible_box;
                    if (consider_rescale_workspace_buffer(i, j, visible_damage))
                    {
                        visible_damage |= visible_box;
                    }

                    if (!visible_damage.empty())
                    {
                        scene::render_pass_params_t params;
                        params.instances = &instances[i][j];
                        params.damage    = std::move(visible_damage);
                        params.reference_output = self->wall->output;
                        params.target = self->aux_buffers[i][j];
                        scene::run_render_pass(params, scene::RPASS_EMIT_SIGNALS);
                        self->aux_buffer_damage[i][j] ^= visible_damage;
                    }
                }
            }

            // Render the wall
            instructions.push_back(scene::render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = damage & self->get_bounding_box(),
                });

            damage ^= self->get_bounding_box();
        }

        static gl_geometry scale_fbox(wf::geometry_t A, wf::geometry_t B, wf::geometry_t box)
        {
            const float px  = 1.0 * (box.x - A.x) / A.width;
            const float py  = 1.0 * (box.y - A.y) / A.height;
            const float px2 = 1.0 * (box.x + box.width - A.x) / A.width;
            const float py2 = 1.0 * (box.y + box.height - A.y) / A.height;
            return gl_geometry{
                B.x + B.width * px,
                B.y + B.height * py,
                B.x + B.width * px2,
                B.y + B.height * py2,
            };
        }

        void render(const wf::render_target_t& target, const wf::region_t& region) override
        {
            OpenGL::render_begin(target);
            for (auto& box : region)
            {
                target.logic_scissor(wlr_box_from_pixman_box(box));
                OpenGL::clear(self->wall->background_color);
                for (int i = 0; i < (int)self->workspaces.size(); i++)
                {
                    for (int j = 0; j < (int)self->workspaces[i].size(); j++)
                    {
                        auto box = get_workspace_rect({i, j});
                        auto A   = self->wall->viewport;
                        auto B   = self->get_bounding_box();
                        gl_geometry render_geometry = scale_fbox(A, B, box);
                        auto& buffer = self->aux_buffers[i][j];

                        float dim = self->wall->get_color_for_workspace({i, j});
                        const glm::vec4 color = glm::vec4(dim, dim, dim, 1.0);

                        if (!buffer.subbuffer.has_value())
                        {
                            OpenGL::render_transformed_texture({buffer.tex},
                                render_geometry, {}, target.get_orthographic_projection(), color);
                        } else
                        {
                            // The 0.999f come from trying to avoid floating-point artifacts
                            const gl_geometry tex_geometry = {
                                0.0f,
                                1.0f - 0.999f * buffer.subbuffer->height / buffer.viewport_height,
                                0.999f * buffer.subbuffer->width / buffer.viewport_width,
                                1.0f,
                            };

                            OpenGL::render_transformed_texture({buffer.tex},
                                render_geometry, tex_geometry,
                                target.get_orthographic_projection(),
                                color, OpenGL::TEXTURE_USE_TEX_GEOMETRY);
                        }
                    }
                }
            }

            OpenGL::render_end();
            self->wall->render_wall(target, region);
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
    std::map<std::pair<int, int>, float> render_colors;

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

                aux_buffers[i][j].geometry = workspaces[i][j]->get_bounding_box();
                aux_buffers[i][j].scale    = wall->output->handle->scale;
                aux_buffers[i][j].wl_transform = WL_OUTPUT_TRANSFORM_NORMAL;
                aux_buffers[i][j].transform    = get_output_matrix_from_transform(
                    aux_buffers[i][j].wl_transform);

                auto size =
                    aux_buffers[i][j].framebuffer_box_from_geometry_box(aux_buffers[i][j].geometry);
                OpenGL::render_begin();
                aux_buffers[i][j].allocate(size.width, size.height);
                OpenGL::render_end();

                aux_buffer_damage[i][j] |= aux_buffers[i][j].geometry;
                aux_buffer_current_scale[i][j] = 1.0;
            }
        }
    }

    ~workspace_wall_node_t()
    {
        OpenGL::render_begin();
        for (auto& [_, buffers] : aux_buffers)
        {
            for (auto& [_, buffer] : buffers)
            {
                buffer.release();
            }
        }

        OpenGL::render_end();
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

    // Buffers keeping the contents of almost-static workspaces
    per_workspace_map_t<wf::render_target_t> aux_buffers;
    // Damage accumulated for those buffers
    per_workspace_map_t<wf::region_t> aux_buffer_damage;
    // Current rendering scale for the workspace
    per_workspace_map_t<float> aux_buffer_current_scale;
};

workspace_wall_t::workspace_wall_t(wf::output_t *_output) : output(_output)
{
    this->viewport = get_wall_rectangle();
}

workspace_wall_t::~workspace_wall_t()
{
    stop_output_renderer(false);
}

void workspace_wall_t::set_background_color(const wf::color_t& color)
{
    this->background_color = color;
}

void workspace_wall_t::set_gap_size(int size)
{
    this->gap_size = size;
}

void workspace_wall_t::set_viewport(const wf::geometry_t& viewport_geometry)
{
    this->viewport = viewport_geometry;
    if (render_node)
    {
        scene::damage_node(
            this->render_node, this->render_node->get_bounding_box());
    }
}

wf::geometry_t workspace_wall_t::get_viewport() const
{
    return viewport;
}

void workspace_wall_t::render_wall(
    const wf::render_target_t& fb, const wf::region_t& damage)
{
    wall_frame_event_t data{fb};
    this->emit(&data);
}

void workspace_wall_t::start_output_renderer()
{
    wf::dassert(render_node == nullptr, "Starting workspace-wall twice?");
    render_node = std::make_shared<workspace_wall_node_t>(this);
    scene::add_front(wf::get_core().scene(), render_node);
}

void workspace_wall_t::stop_output_renderer(bool reset_viewport)
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

wf::geometry_t workspace_wall_t::get_workspace_rectangle(
    const wf::point_t& ws) const
{
    auto size = this->output->get_screen_size();

    return {ws.x * (size.width + gap_size), ws.y * (size.height + gap_size),
        size.width, size.height};
}

wf::geometry_t workspace_wall_t::get_wall_rectangle() const
{
    auto size = this->output->get_screen_size();
    auto workspace_size = this->output->wset()->get_workspace_grid_size();

    return {-gap_size, -gap_size,
        workspace_size.width * (size.width + gap_size) + gap_size,
        workspace_size.height * (size.height + gap_size) + gap_size};
}

void workspace_wall_t::set_ws_dim(const wf::point_t& ws, float value)
{
    render_colors[{ws.x, ws.y}] = value;
    if (render_node)
    {
        scene::damage_node(render_node, render_node->get_bounding_box());
    }
}

float workspace_wall_t::get_color_for_workspace(wf::point_t ws)
{
    auto it = render_colors.find({ws.x, ws.y});
    if (it == render_colors.end())
    {
        return 1.0;
    }

    return it->second;
}

std::vector<wf::point_t> workspace_wall_t::get_visible_workspaces(
    wf::geometry_t viewport) const
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
} // namespace wf
