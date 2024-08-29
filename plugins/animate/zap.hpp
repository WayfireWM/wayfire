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

#include "animate.hpp"
#include <memory>
#include <wayfire/plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/output.hpp>

namespace wf
{
namespace zap
{
static const std::string zap_transformer_name = "zap-transformer";
using namespace wf::animation;
class zap_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
};
class zap_animation : public animate::animation_base_t
{
    wayfire_view view;
    animate::animation_type type;
    wf::zap::zap_animation_t progression;

  public:

    void init(wayfire_view view, wf::animation_description_t dur, animate::animation_type type) override
    {
        this->view = view;
        this->type = type;
        this->progression = wf::zap::zap_animation_t(wf::create_option<wf::animation_description_t>(dur));

        if (type & WF_ANIMATE_HIDING_ANIMATION)
        {
            this->progression.reverse();
        }

        this->progression.start();

        auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(view);
        view->get_transformed_node()->add_transformer(
            tr, wf::TRANSFORMER_HIGHLEVEL, zap_transformer_name);
    }

    bool step() override
    {
        auto transform = view->get_transformed_node()
            ->get_transformer<wf::scene::view_2d_transformer_t>(zap_transformer_name);
        auto progress = this->progression.progress();
        auto progress_pt_one   = std::clamp(progress, 0.0, 1.0 / 3.0) * 3.0;
        auto progress_pt_two   = (std::clamp(progress, 1.0 / 3.0, (1.0 / 3.0) * 2.0) - 1.0 / 3.0) * 3.0;
        auto progress_pt_three = (std::clamp(progress, (1.0 / 3.0) * 2.0, 1.0) - (1.0 / 3.0) * 2.0) * 3.0;
        transform->alpha   = progress_pt_one;
        transform->scale_x = 0.01 + progress_pt_two * 0.99;
        transform->scale_y = 0.01 + progress_pt_three * 0.99;

        return progression.running();
    }

    void reverse() override
    {
        this->progression.reverse();
    }

    ~zap_animation()
    {
        view->get_transformed_node()->rem_transformer(zap_transformer_name);
    }
};
}
}
