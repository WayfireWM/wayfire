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


static const char *squeeze_vert_source =
    R"(
#version 100

attribute mediump vec2 position;
attribute mediump vec2 uv_in;

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

precision mediump float;

varying highp vec2 uv;
uniform mediump float progress;
uniform mediump vec4 src_box;
uniform mediump vec4 target_box;
uniform int upward;

void main()
{
    float y;
    vec2 uv_squeeze;
    float inv_w = 1.0 / (src_box.z - src_box.x);
    float inv_h = 1.0 / (src_box.w - src_box.y);
    float progress_pt_one = clamp(progress, 0.0, 0.5) * 2.0;
    float progress_pt_two = (clamp(progress, 0.5, 1.0) - 0.5) * 2.0;

    uv_squeeze.x = (uv.x * inv_w) - (inv_w - 1.0);
    uv_squeeze.x += inv_w - inv_w * src_box.z;
    uv_squeeze.y = (uv.y * inv_h) - (inv_h - 1.0);
    uv_squeeze.y += inv_h * src_box.y;

    if (upward == 1)
    {
        y = uv.y;
        uv_squeeze.y += -progress_pt_two * (inv_h - target_box.w);
    } else
    {
        y = 1.0 - uv.y;
        uv_squeeze.y -= -progress_pt_two * (src_box.y + target_box.y + target_box.w);
    }

    float sigmoid = 1.0 / (1.0 + pow(2.718, -((y * inv_h) * 6.0 - 3.0)));
    sigmoid *= progress_pt_one * (src_box.x - target_box.x);

    uv_squeeze.x += sigmoid * inv_w;
    uv_squeeze.x *= (y * (1.0 / (target_box.z - target_box.x)) * progress_pt_one) + 1.0;

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
            const wf::render_target_t& target, wf::region_t& damage) override
        {
            instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & self->get_bounding_box(),
                    });
        }

        void transform_damage_region(wf::region_t& damage) override
        {
            damage |= wf::region_t{self->animation_geometry};
        }

        void render(const wf::render_target_t& target,
            const wf::region_t& damage) override
        {
            auto src_box = self->get_children_bounding_box();
            auto src_tex = wf::scene::transformer_render_instance_t<squeezimize_transformer>::get_texture(
                1.0);
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

            const float vertex_data_pos[] = {
                1.0f * self->animation_geometry.x,
                1.0f * self->animation_geometry.y + self->animation_geometry.height,
                1.0f * self->animation_geometry.x + self->animation_geometry.width,
                1.0f * self->animation_geometry.y + self->animation_geometry.height,
                1.0f * self->animation_geometry.x + self->animation_geometry.width,
                1.0f * self->animation_geometry.y,
                1.0f * self->animation_geometry.x, 1.0f * self->animation_geometry.y,
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

            OpenGL::render_begin(target);
            self->program.use(wf::TEXTURE_TYPE_RGBA);
            self->program.uniformMatrix4f("matrix", target.get_orthographic_projection());
            self->program.attrib_pointer("position", 2, 0, vertex_data_pos);
            self->program.attrib_pointer("uv_in", 2, 0, vertex_data_uv);
            self->program.uniform1i("upward", self->upward);
            self->program.uniform1f("progress", progress);
            self->program.uniform4f("src_box", src_box_pos);
            self->program.uniform4f("target_box", target_box_pos);
            self->program.set_active_texture(src_tex);
            for (const auto& box : damage)
            {
                target.logic_scissor(wlr_box_from_pixman_box(box));
                GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
            }

            OpenGL::render_end();
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
        OpenGL::render_begin();
        program.compile(squeeze_vert_source, squeeze_frag_source);
        OpenGL::render_end();

        auto src_box = view->get_bounding_box();
        auto output  = view->get_output();
        this->upward = ((src_box.y > minimize_target.y) ||
            ((src_box.y < 0) && (minimize_target.y < output->get_relative_geometry().height / 2)));
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
        program.free_resources();
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
        auto node = std::make_shared<wf::squeezimize::squeezimize_transformer>(view, dur, hint, bbox);
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
