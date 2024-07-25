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

#include "animate.hpp"


static std::string transformer_name = "animation-squeezimize";

wf::option_wrapper_t<wf::animation_description_t> squeezimize_duration{"animate/squeezimize_duration"};
wf::option_wrapper_t<int> squeezimize_line_height{"animate/squeezimize_line_height"};

namespace wf
{
namespace squeezimize
{
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
    wayfire_view view;
    bool last_direction = false;
    wf::output_t *output;
    wf::geometry_t minimize_target;
    squeezimize_animation_t progression{squeezimize_duration};

    class simple_node_render_instance_t : public wf::scene::transformer_render_instance_t<transformer_base_node_t>
    {
        wf::signal::connection_t<node_damage_signal> on_node_damaged =
            [=] (node_damage_signal *ev)
        {
            push_to_parent(wf::region_t{self->output->get_relative_geometry()});
        };

        squeezimize_transformer *self;
        wayfire_view view;
        damage_callback push_to_parent;

      public:
        simple_node_render_instance_t(squeezimize_transformer *self, damage_callback push_damage,
            wayfire_view view) : wf::scene::transformer_render_instance_t<transformer_base_node_t>(self,
                push_damage,
                view->get_output())
        {
            this->self = self;
            this->view = view;
            this->push_to_parent = push_damage;
            self->connect(&on_node_damaged);
        }

        ~simple_node_render_instance_t()
        {}

        void schedule_instructions(
            std::vector<render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::region_t& damage)
        {
            instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage,
                    });
        }

        void transform_damage_region(wf::region_t& damage) override
        {
            damage |= wf::region_t{self->output->get_relative_geometry()};
        }

        void render(const wf::render_target_t& target,
            const wf::region_t& region)
        {
            auto src_box = self->get_children_bounding_box();
            auto src_tex = wf::scene::transformer_render_instance_t<transformer_base_node_t>::get_texture(
                1.0);
            auto progress = self->progression.progress();
            bool upward   = ((src_box.y > self->minimize_target.y) ||
                ((src_box.y < 0) &&
                    (self->minimize_target.y < self->output->get_relative_geometry().height / 2)));
            int max_height;
            if (upward)
            {
                max_height = self->minimize_target.y + self->minimize_target.height + src_box.y +
                    src_box.height;
            } else
            {
                max_height = self->minimize_target.y + src_box.y;
            }

            int line_height = std::min(max_height, int(squeezimize_line_height));

            OpenGL::render_begin(target);
            for (int i = 0;
                 i < max_height;
                 i += line_height)
            {
                gl_geometry src_geometry = {(float)src_box.x, (float)src_box.y,
                    (float)src_box.x + src_box.width, (float)src_box.y + src_box.height};
                float direction;
                if (upward)
                {
                    direction = (float)(max_height - i) / max_height;
                } else
                {
                    direction = (float)i / max_height;
                }

                auto s1 = 1.0 / (1.0 + pow(2.718, -(direction * 6.0 - 3.0)));
                s1 *= progress * 0.5;
                auto squeeze_region = wf::region_t{self->output->get_relative_geometry()};
                auto squeeze_box    = src_box;
                squeeze_box.y = i;
                squeeze_box.height = line_height;
                squeeze_region    &=
                    wf::region_t{wf::geometry_t{self->output->get_relative_geometry().x, squeeze_box.y,
                        self->output->get_relative_geometry().width, squeeze_box.height}};

                if ((src_box.y > self->minimize_target.y) ||
                    ((src_box.y < 0) &&
                     (self->minimize_target.y < self->output->get_relative_geometry().height / 2)))
                {
                    src_geometry.y1 +=
                        ((std::clamp(progress, 0.5,
                            1.0) - 0.5) * 2.0) *
                        ((self->minimize_target.y + self->minimize_target.height) -
                            (src_box.y + src_box.height));
                    src_geometry.y2 +=
                        ((std::clamp(progress, 0.5,
                            1.0) - 0.5) * 2.0) *
                        ((self->minimize_target.y + self->minimize_target.height) -
                            (src_box.y + src_box.height));
                } else
                {
                    src_geometry.y1 +=
                        ((std::clamp(progress, 0.5,
                            1.0) - 0.5) * 2.0) *
                        ((self->minimize_target.y) -
                            (src_box.y));
                    src_geometry.y2 +=
                        ((std::clamp(progress, 0.5,
                            1.0) - 0.5) * 2.0) *
                        ((self->minimize_target.y) -
                            (src_box.y));
                }

                src_geometry.x1 += s1 * (src_box.width - self->minimize_target.width);
                src_geometry.x1 +=
                    std::clamp(progress, 0.0,
                        0.5) * 2.0 * direction *
                    (self->minimize_target.x - src_geometry.x1);
                src_geometry.x2 -= s1 * (src_box.width - self->minimize_target.width);
                src_geometry.x2 +=
                    std::clamp(progress, 0.0,
                        0.5) * 2.0 * direction *
                    ((self->minimize_target.x + self->minimize_target.width) - src_geometry.x2);

                for (const auto& box : squeeze_region)
                {
                    target.logic_scissor(wlr_box_from_pixman_box(box));
                    OpenGL::render_transformed_texture(src_tex, src_geometry, {},
                        target.get_orthographic_projection(), glm::vec4(1.0), 0);
                }
            }

            OpenGL::render_end();
        }
    };

    squeezimize_transformer(wayfire_view view,
        wf::geometry_t minimize_target) : wf::scene::view_2d_transformer_t(view)
    {
        this->view = view;
        this->minimize_target = minimize_target;
        if (view->get_output())
        {
            output = view->get_output();
            output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        }
    }

    wf::geometry_t get_bounding_box() override
    {
        return output->get_relative_geometry();
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        view->damage();
        output->render->damage_whole();
    };

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, view));
    }

    void init_animation(bool squeeze)
    {
        if (this->progression.running())
        {
            this->progression.reverse();
        } else
        {
            if ((squeeze && !this->progression.get_direction()) ||
                (!squeeze && this->progression.get_direction()))
            {
                this->progression.reverse();
            }

            this->progression.start();
        }

        last_direction = squeeze;
    }

    virtual ~squeezimize_transformer()
    {
        if (output)
        {
            output->render->rem_effect(&pre_hook);
        }
    }
};
}
}

class squeezimize_animation : public animation_base
{
    wayfire_view view;

  public:
    void init(wayfire_view view, wf::animation_description_t dur, wf_animation_type type) override
    {
        this->view = view;
        pop_transformer(view);
        auto toplevel = wf::toplevel_cast(view);
        wf::dassert(toplevel != nullptr, "We cannot minimize non-toplevel views!");
        auto hint = toplevel->get_minimize_hint();
        auto tmgr = view->get_transformed_node();
        auto node = std::make_shared<wf::squeezimize::squeezimize_transformer>(view, hint);
        tmgr->add_transformer(node, wf::TRANSFORMER_2D, transformer_name);
        node->init_animation(type & HIDING_ANIMATION);
    }

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(transformer_name))
        {
            view->get_transformed_node()->rem_transformer(transformer_name);
        }
    }

    bool step() override
    {
        if (!view)
        {
            return false;
        }

        auto tmgr = view->get_transformed_node();
        if (!tmgr)
        {
            return false;
        }

        if (auto tr = tmgr->get_transformer<wf::squeezimize::squeezimize_transformer>(transformer_name))
        {
            auto running = tr->progression.running();
            if (!running)
            {
                pop_transformer(view);
                return false;
            }

            return running;
        }

        return false;
    }

    void reverse() override
    {
        if (auto tr =
                view->get_transformed_node()->get_transformer<wf::squeezimize::squeezimize_transformer>(
                    transformer_name))
        {
            tr->progression.reverse();
        }
    }
};
