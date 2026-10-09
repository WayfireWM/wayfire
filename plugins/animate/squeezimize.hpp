/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Scott Moreau <oreaus@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#ifdef WF_USE_CONFIG_H
    #include <config.h>
#else
    #include <wayfire/config.h>
#endif
#include <wayfire/core.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/util/duration.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "animate.hpp"

#if WF_HAS_VULKANFX
    #include <wayfire/vulkan.hpp>
    #include "shaders/squeezimize.vert.h"
    #include "shaders/squeezimize.frag.h"
    #include "shaders/squeezimize-horiz.frag.h"
#endif


static const char *squeeze_vert_source =
    R"(
#version 100

attribute highp vec2 position;
attribute highp vec2 uv_in;

uniform mat4 matrix;

varying highp vec2 uv;

void main() {
    uv = uv_in;
    gl_Position = matrix * vec4(position, 0.0, 1.0);
}
)";

static const char *squeeze_frag_source =
    R"(
#version 100
@builtin_ext@
@builtin@

precision highp float;

varying highp vec2 uv;
uniform highp float progress;
uniform highp vec4 src_box;
uniform highp vec4 target_box;
uniform int upward;

void main()
{
    float y, sigmoid;
    vec2 uv_squeeze;
    float inv_w = 1.0 / (src_box.z - src_box.x);
    float inv_h = 1.0 / (src_box.w - src_box.y);
    float progress_pt_one = pow(clamp(progress, 0.0, 0.25) * 4.0, 2.0);
    float progress_pt_two = pow(progress, 2.0);

    uv_squeeze.x = inv_w * (uv.x - src_box.x);
    uv_squeeze.y = inv_h * (uv.y - 1.0 + src_box.w);

    if (upward == 1)
    {
        y = uv.y;
        uv_squeeze.y += -progress_pt_two * (inv_h - target_box.w);
        sigmoid = 1.0 / (1.0 + pow(2.718, -(y * (1.0 / (src_box.w - target_box.w)) * 15.0 - 10.0)));
    } else
    {
        y = 1.0 - uv.y;
        uv_squeeze.y -= -progress_pt_two * (inv_h - target_box.y + target_box.w);
        sigmoid = 1.0 / (1.0 + pow(2.718, -(y * (1.0 / (target_box.w - src_box.y)) * 15.0 - 10.0)));
    }

    float t = sigmoid * progress_pt_one;
    float sx0 = mix(src_box.x, target_box.x, t);
    float sx1 = mix(src_box.z, target_box.z, t);
    uv_squeeze.x = (uv.x - sx0) / (sx1 - sx0);

    if (uv_squeeze.x < 0.0 || uv_squeeze.y < 0.0 ||
        uv_squeeze.x > 1.0 || uv_squeeze.y > 1.0)
    {
        discard;
    }

    gl_FragColor = get_pixel(uv_squeeze);
}
)";


static const char *squeeze_frag_source_horiz =
    R"(
#version 100
@builtin_ext@
@builtin@

precision mediump float;

varying highp vec2 uv;
uniform mediump float progress;
uniform mediump vec4 src_box;
uniform mediump vec4 target_box;
uniform int upward;

void main()
{
    float y, sigmoid;
    vec2 uv_squeeze;
    float inv_w = 1.0 / (src_box.z - src_box.x);
    float inv_h = 1.0 / (src_box.w - src_box.y);
    float progress_pt_one = pow(clamp(progress, 0.0, 0.25) * 4.0, 2.0);
    float progress_pt_two = pow(progress, 2.0);

    uv_squeeze.x = inv_w * (uv.x - src_box.x);
    uv_squeeze.y = inv_h * (1.0 - uv.y - src_box.y);

    if (upward == 1)
    {
        y = 1.0 - uv.x;
        uv_squeeze.x += progress_pt_two * (inv_w - target_box.z);
        sigmoid = 1.0 / (1.0 + pow(2.718, -(y * (1.0 / (src_box.z - target_box.z)) * 15.0 - 10.0)));
    } else
    {
        y = uv.x;
        uv_squeeze.x -= progress_pt_two * (inv_w - target_box.x + target_box.z);
        sigmoid = 1.0 / (1.0 + pow(2.718, -(y * (1.0 / (target_box.z - src_box.x)) * 15.0 - 10.0)));
    }

    float t = sigmoid * progress_pt_one;
    float sy0 = mix(src_box.y, 1.0 - target_box.w, t);
    float sy1 = mix(src_box.w, 1.0 - target_box.y, t);
    uv_squeeze.y = (uv.y - sy1) / (sy0 - sy1);
    uv_squeeze.y = 1.0 - uv_squeeze.y;

    if (uv_squeeze.x < 0.0 || uv_squeeze.y < 0.0 ||
        uv_squeeze.x > 1.0 || uv_squeeze.y > 1.0)
    {
        discard;
    }

    gl_FragColor = get_pixel(uv_squeeze);
}
)";

namespace wf
{
namespace squeezimize
{
static std::string squeezimize_transformer_name = "animation-squeezimize";
using namespace wf::scene;
using namespace wf::animation;
class squeezimize_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t squeeze{*this};
};
class squeezimize_transformer : public wf::scene::view_2d_transformer_t
{
  public:
    OpenGL::program_t program;
    wf::geometry_t minimize_target;
    wf::geometry_t animation_geometry;
    squeezimize_animation_t progression;
    bool upward = false;
    bool horiz  = false;

    class simple_node_render_instance_t : public wf::scene::transformer_render_instance_t<squeezimize_transformer>
    {
        wf::signal::connection_t<node_damage_signal> on_node_damaged =
            [=] (node_damage_signal *ev)
        {
            push_to_parent(ev->region);
        };

        damage_callback push_to_parent;

      public:
        simple_node_render_instance_t(squeezimize_transformer *self, damage_callback push_damage,
            wf::output_t *output) : wf::scene::transformer_render_instance_t<squeezimize_transformer>(self,
                push_damage,
                output)
        {
            this->push_to_parent = push_damage;
            self->connect(&on_node_damaged);
        }

        ~simple_node_render_instance_t()
        {}

        void schedule_instructions(
            std::vector<render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage) override
        {
            instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & self->get_bounding_box(),
                    });
        }

        void transform_damage_region(wf::regionf_t& damage) override
        {
            damage |= self->animation_geometry;
        }

    #if WF_HAS_VULKANFX
    struct vulkan_push_constants_t
    {
        glm::mat4 matrix;
        glm::vec4 src_box;
        glm::vec4 target_box;
        float progress;
        int upward;
    };

    class vulkan_state_t : public wf::custom_data_t
    {
      public:
        std::shared_ptr<wf::vk::graphics_pipeline_t> pipeline;
    };

    std::shared_ptr<wf::vk::gpu_buffer_t> vulkan_vertex_buffer;

    vulkan_state_t& ensure_vk(wf::vulkan_render_state_t& state)
    {
        if (auto data = state.get_data<vulkan_state_t>())
        {
            return *data;
        }

        auto vs = state.get_context()->load_shader_module(
            squeezimize_vert_data, sizeof(squeezimize_vert_data));
        auto fs = state.get_context()->load_shader_module(
            self->horiz ? squeezimize_horiz_frag_data : squeezimize_frag_data,
            self->horiz ? sizeof(squeezimize_horiz_frag_data) : sizeof(squeezimize_frag_data));

        wf::vk::pipeline_params_t params{};
        params.shaders = {
            {.stage = VK_SHADER_STAGE_VERTEX_BIT, .shader = vs},
            {.stage = VK_SHADER_STAGE_FRAGMENT_BIT, .shader = fs},
        };

        // One descriptor set for the uv texture.
        params.descriptor_set_layouts = {wf::vk::pipeline_params_t::texture_descriptor_set_t{}};
        params.push_constants = {
            VkPushConstantRange{
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset     = 0,
                .size       = sizeof(vulkan_push_constants_t),
            },
        };

        params.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        params.vertex_input_description = {{
            .binding   = 0,
            .stride    = sizeof(float) * 4,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        }};
        params.vertex_attribute_description = {
            {
                .location = 0,
                .binding  = 0,
                .format   = VK_FORMAT_R32G32_SFLOAT,
                .offset   = 0,
            },
            {
                .location = 1,
                .binding  = 0,
                .format   = VK_FORMAT_R32G32_SFLOAT,
                .offset   = sizeof(float) * 2,
            },
        };

        auto data = std::make_unique<vulkan_state_t>();
        data->pipeline = std::make_shared<wf::vk::graphics_pipeline_t>(state.get_context(), params);
        auto ptr = data.get();
        state.store_data<vulkan_state_t>(std::move(data));
        return *ptr;
    }

    void upload_vertex_data(wf::vulkan_render_state_t& state,
        const float *vertex_data_pos, const float *vertex_data_uv)
    {
        /* Interleaved vertex data: [pos.x, pos.y, uv.x, uv.y]. */
        const size_t floats_per_vertex = 4;
        float unified_buffer[floats_per_vertex * 4];
        for (size_t i = 0; i < 4; i++)
        {
            unified_buffer[floats_per_vertex * i]     = vertex_data_pos[2 * i];
            unified_buffer[floats_per_vertex * i + 1] = vertex_data_pos[2 * i + 1];
            unified_buffer[floats_per_vertex * i + 2] = vertex_data_uv[2 * i];
            unified_buffer[floats_per_vertex * i + 3] = vertex_data_uv[2 * i + 1];
        }

        const VkDeviceSize total_size = sizeof(unified_buffer);

        /* Reuse the buffer between frames when possible, to avoid reallocations. */
        if (!vulkan_vertex_buffer || (vulkan_vertex_buffer->get_size() < total_size) ||
            (vulkan_vertex_buffer.use_count() != 1))
        {
            vulkan_vertex_buffer = state.get_context()->create_buffer(total_size,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        }

        vulkan_vertex_buffer->write(unified_buffer, total_size);
    }
#endif

    void render(const render_instruction_t& data) override
        {
            auto src_box  = self->get_children_bounding_box();
            auto progress = self->progression.progress();
            static const float vertex_data_uv[] = {
                0.0f, 0.0f,
                1.0f, 0.0f,
                1.0f, 1.0f,
                0.0f, 1.0f,
            };

            self->animation_geometry.x     = std::min(src_box.x, self->minimize_target.x);
            self->animation_geometry.y     = std::min(src_box.y, self->minimize_target.y);
            self->animation_geometry.width =
                std::max(std::max(std::max(src_box.width,
                    self->minimize_target.width),
                    (self->minimize_target.x + self->minimize_target.width) - src_box.x),
                    (src_box.x + src_box.width) - self->minimize_target.x);
            self->animation_geometry.height =
                std::max(std::max(std::max(src_box.height,
                    self->minimize_target.height),
                    (self->minimize_target.y + self->minimize_target.height) - src_box.y),
                    (src_box.y + src_box.height) - self->minimize_target.y);

            const float geometry_x     = (float)self->animation_geometry.x;
            const float geometry_y     = (float)self->animation_geometry.y;
            const float geometry_width = (float)self->animation_geometry.width;
            const float geometry_height = (float)self->animation_geometry.height;

            const float vertex_data_pos[] = {
                geometry_x,
                geometry_y + geometry_height,
                geometry_x + geometry_width,
                geometry_y + geometry_height,
                geometry_x + geometry_width,
                geometry_y,
                geometry_x, geometry_y,
            };

            const glm::vec4 src_box_pos{
                float(src_box.x - self->animation_geometry.x) / self->animation_geometry.width,
                float(src_box.y - self->animation_geometry.y) / self->animation_geometry.height,
                float((src_box.x - self->animation_geometry.x) + src_box.width) /
                self->animation_geometry.width,
                float((src_box.y - self->animation_geometry.y) + src_box.height) /
                self->animation_geometry.height
            };

            const glm::vec4 target_box_pos{
                float(self->minimize_target.x - self->animation_geometry.x) / self->animation_geometry.width,
                float(self->minimize_target.y - self->animation_geometry.y) / self->animation_geometry.height,
                float((self->minimize_target.x - self->animation_geometry.x) + self->minimize_target.width) /
                self->animation_geometry.width,
                float((self->minimize_target.y - self->animation_geometry.y) + self->minimize_target.height) /
                self->animation_geometry.height
            };

            data.pass->custom_gles_subpass(data.target, [&]
            {
                auto src_tex = wf::gles_texture_t{this->get_texture(1.0)};
                self->program.use(wf::TEXTURE_TYPE_RGBA);
                self->program.uniformMatrix4f("matrix",
                    wf::gles::render_target_orthographic_projection(data.target));
                self->program.attrib_pointer("position", 2, 0, vertex_data_pos);
                self->program.attrib_pointer("uv_in", 2, 0, vertex_data_uv);
                self->program.uniform1i("upward", self->upward);
                self->program.uniform1f("progress", progress);
                self->program.uniform4f("src_box", src_box_pos);
                self->program.uniform4f("target_box", target_box_pos);
                self->program.set_active_texture(src_tex);
                gles::for_each_scissor_rect(data.target, data.damage, [&]
                {
                    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
                });
            });

#if WF_HAS_VULKANFX
            data.pass->custom_vulkan_subpass([&] (wf::vulkan_render_state_t& state,
                                                  wf::vk::command_buffer_t& cmd_buf)
            {
                auto& vk_state = ensure_vk(state);
                upload_vertex_data(state, vertex_data_pos, vertex_data_uv);

                auto texture   = this->get_texture(1.0);
                auto tex_dset  = state.get_descriptor_pool()->get_descriptor_set(cmd_buf, texture);
                wf::vk::texture_sampling_params_t sampling{texture};
                wf::vk::pipeline_specialization_t specialization{};
                specialization.add_specialization_for_texture(texture);

                auto [layout, _] = cmd_buf.bind_pipeline(vk_state.pipeline, data.target,
                    specialization);
                cmd_buf.set_full_viewport(data.target);
                cmd_buf.bind_texture(texture);

                vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                    0, 1, &tex_dset, 0, nullptr);

                vulkan_push_constants_t push_constants{};
                push_constants.matrix = wf::vk::render_target_transform(data.target);
                push_constants.progress    = progress;
                push_constants.src_box     = src_box_pos;
                push_constants.target_box  = target_box_pos;
                push_constants.upward      = self->upward ? 1 : 0;
                vkCmdPushConstants(cmd_buf, layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(vulkan_push_constants_t), &push_constants);

                VkBuffer vertex_buffer = vulkan_vertex_buffer->get_buffer();
                VkDeviceSize vertex_offset = 0;
                vkCmdBindVertexBuffers(cmd_buf, 0, 1, &vertex_buffer, &vertex_offset);
                cmd_buf.bind_buffer(vulkan_vertex_buffer);

                cmd_buf.for_each_scissor_rect(data.target, (data.damage & data.target.geometry), [&]
                {
                    vkCmdDraw(cmd_buf, 4, 1, 0, 0);
                });
            });
#endif
        }
    };

    squeezimize_transformer(wayfire_view view, wf::animation_description_t duration,
        wf::geometry_t minimize_target, wf::geometry_t bbox) : wf::scene::view_2d_transformer_t(view)
    {
        this->progression     = squeezimize_animation_t{wf::create_option<>(duration)};
        this->minimize_target = minimize_target;
        /* If there is no minimize target set, minimize to the bottom center of the output */
        if ((this->minimize_target.width <= 0) || (this->minimize_target.height <= 0))
        {
            if (auto output = view->get_output())
            {
                auto og = output->get_relative_geometry();
                this->minimize_target.x     = og.width / 2 - 50;
                this->minimize_target.y     = og.height;
                this->minimize_target.width = 100;
                this->minimize_target.height = 50;
            }
        }

        animation_geometry.x     = std::min(bbox.x, this->minimize_target.x);
        animation_geometry.y     = std::min(bbox.y, this->minimize_target.y);
        animation_geometry.width =
            std::max(std::max(std::max(bbox.width,
                this->minimize_target.width),
                (this->minimize_target.x + this->minimize_target.width) - bbox.x),
                (bbox.x + bbox.width) - this->minimize_target.x);
        animation_geometry.height =
            std::max(std::max(std::max(bbox.height,
                this->minimize_target.height),
                (this->minimize_target.y + this->minimize_target.height) - bbox.y),
                (bbox.y + bbox.height) - this->minimize_target.y);

        bool horiz;
        // auto src_box = view->get_bounding_box();
        auto output = view->get_output();
        auto geom   = output->get_relative_geometry();

        // check which edge the target is closest to
        double x  = this->minimize_target.x + this->minimize_target.width / 2.0;
        double y  = this->minimize_target.y + this->minimize_target.height / 2.0;
        double y2 = y * geom.width / geom.height;
        if (x < y2)
        {
            // bottom left part of the screen
            if (x < geom.width - y2)
            {
                // left edge
                horiz = true;
                this->upward = true;
            } else
            {
                // bottom edge
                horiz = false;
                this->upward = false;
            }
        } else
        {
            // top right part of the screen
            if (x > geom.width - y2)
            {
                // right edge
                horiz = true;
                this->upward = false;
            } else
            {
                // top edge
                horiz = false;
                this->upward = true;
            }
        }

        this->horiz = horiz;

        wf::gles::run_in_context_if_gles([&]
        {
            program.compile(squeeze_vert_source, horiz ? squeeze_frag_source_horiz : squeeze_frag_source);
        });
    }

    wf::geometry_t get_bounding_box() override
    {
        return this->animation_geometry;
    }

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, shown_on));
    }

    void init_animation(bool squeeze)
    {
        if (!squeeze)
        {
            this->progression.reverse();
        }

        this->progression.start();
    }

    virtual ~squeezimize_transformer()
    {
        wf::gles::run_in_context_if_gles([&]
        {
            program.free_resources();
        });
    }
};

class squeezimize_animation : public animate::animation_base_t
{
    wayfire_view view;

  public:
    void init(wayfire_view view, wf::animation_description_t dur, animate::animation_type type) override
    {
        this->view = view;
        pop_transformer(view);
        auto bbox     = view->get_transformed_node()->get_children_bounding_box();
        auto toplevel = wf::toplevel_cast(view);
        wf::dassert(toplevel != nullptr, "We cannot minimize non-toplevel views!");
        auto hint = toplevel->get_minimize_hint();
        auto tmgr = view->get_transformed_node();
        auto node = std::make_shared<wf::squeezimize::squeezimize_transformer>(view, dur,
            wf::from_integer_box(hint), bbox);
        tmgr->add_transformer(node, wf::TRANSFORMER_HIGHLEVEL + 1, squeezimize_transformer_name);
        node->init_animation(type & WF_ANIMATE_HIDING_ANIMATION);
    }

    ~squeezimize_animation()
    {
        pop_transformer(this->view);
    }

    void pop_transformer(wayfire_view view)
    {
        view->get_transformed_node()->rem_transformer(squeezimize_transformer_name);
    }

    bool step() override
    {
        auto tmgr = view->get_transformed_node();

        if (auto tr =
                tmgr->get_transformer<squeezimize_transformer>(squeezimize_transformer_name))
        {
            auto running = tr->progression.running();
            if (!running)
            {
                return false;
            }

            return running;
        }

        return false;
    }

    void reverse() override
    {
        if (auto tr =
                view->get_transformed_node()->get_transformer<squeezimize_transformer>(
                    squeezimize_transformer_name))
        {
            tr->progression.reverse();
        }
    }
};
}
}
