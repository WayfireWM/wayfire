/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Scott Moreau
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

#include <memory>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/bindings-repository.hpp>
#include "wayfire/view-helpers.hpp"
#include "wayfire/view-transform.hpp"
#include "wayfire/workspace-set.hpp"

class wayfire_alpha : public wf::plugin_interface_t
{
    wf::option_wrapper_t<wf::keybinding_t> modifier{"alpha/modifier"};
    wf::option_wrapper_t<double> min_value{"alpha/min_value"};
    wf::plugin_activation_data_t grab_interface{
        .name = "alpha",
        .capabilities = wf::CAPABILITY_MANAGE_DESKTOP,
    };

  public:
    void init() override
    {
        min_value.set_callback(min_value_changed);
        wf::get_core().bindings->add_axis(modifier, &axis_cb);
    }

    void update_alpha(wayfire_view view, float delta)
    {
        auto tmgr = view->get_transformed_node();
        if (!tmgr->get_transformer<wf::scene::node_t>("alpha"))
        {
            auto node = std::make_shared<wf::scene::view_2d_transformer_t>(view);
            tmgr->add_transformer(node, wf::TRANSFORMER_2D, "alpha");
        }

        auto transformer = tmgr->get_transformer<wf::scene::view_2d_transformer_t>("alpha");
        auto old_value   = transformer->alpha;
        transformer->alpha = std::clamp(transformer->alpha - delta * 0.003, (double)min_value, 1.0);

        if (transformer->alpha == 1.0)
        {
            return view->get_transformed_node()->rem_transformer("alpha");
        } else if (old_value != transformer->alpha)
        {
            view->damage();
        }
    }

    wf::axis_callback axis_cb = [=] (wlr_pointer_axis_event *ev)
    {
        auto gc = wf::get_core().get_cursor_position();
        auto current_output = wf::get_core().output_layout->get_output_coords_at(gc, gc);
        if (!current_output || !current_output->can_activate_plugin(&grab_interface))
        {
            return false;
        }

        auto view = wf::get_core().get_cursor_focus_view();
        if (!view)
        {
            return false;
        }

        auto layer = wf::get_view_layer(view).value_or(wf::scene::layer::BACKGROUND);
        if (layer == wf::scene::layer::BACKGROUND)
        {
            return false;
        }

        if (ev->orientation == WLR_AXIS_ORIENTATION_VERTICAL)
        {
            update_alpha(view, ev->delta);

            return true;
        }

        return false;
    };

    wf::config::option_base_t::updated_callback_t min_value_changed = [=] ()
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            auto tmgr = view->get_transformed_node();
            auto transformer = tmgr->get_transformer<wf::scene::view_2d_transformer_t>("alpha");
            if (transformer && (transformer->alpha < min_value))
            {
                transformer->alpha = min_value;
                view->damage();
            }
        }
    };

    void fini() override
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            view->get_transformed_node()->rem_transformer("alpha");
        }

        wf::get_core().bindings->rem_binding(&axis_cb);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_alpha);
