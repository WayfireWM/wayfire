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

#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include "wayfire/view-transform.hpp"
#include "wayfire/workspace-manager.hpp"

class wayfire_alpha : public wf::plugin_interface_t
{
    wf::option_wrapper_t<wf::keybinding_t> modifier{"alpha/modifier"};
    wf::option_wrapper_t<double> min_value{"alpha/min_value"};

  public:
    void init() override
    {
        grab_interface->name = "alpha";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP;

        min_value.set_callback(min_value_changed);
        output->add_axis(modifier, &axis_cb);
    }

    void update_alpha(wayfire_view view, float delta)
    {
        wf::view_2D *transformer;
        float alpha;

        if (!view->get_transformer("alpha"))
        {
            view->add_transformer(std::make_unique<wf::view_2D>(view), "alpha");
        }

        transformer =
            dynamic_cast<wf::view_2D*>(view->get_transformer("alpha").get());
        alpha = transformer->alpha;

        alpha -= delta * 0.003;

        if (alpha > 1.0)
        {
            alpha = 1.0;
        }

        if (alpha == 1.0)
        {
            return view->pop_transformer("alpha");
        }

        alpha = std::max(alpha, (float)min_value);
        if (transformer->alpha != alpha)
        {
            transformer->alpha = alpha;
            view->damage();
        }
    }

    wf::axis_callback axis_cb = [=] (wlr_pointer_axis_event *ev)
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        output->deactivate_plugin(grab_interface);

        auto view = wf::get_core().get_cursor_focus_view();
        if (!view)
        {
            return false;
        }

        auto layer = output->workspace->get_view_layer(view);

        if (layer == wf::LAYER_BACKGROUND)
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
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            if (!view->get_transformer("alpha"))
            {
                continue;
            }

            wf::view_2D *transformer =
                dynamic_cast<wf::view_2D*>(view->get_transformer("alpha").get());

            if (transformer->alpha < min_value)
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
            if (!view->get_output() || (view->get_output() == output))
            {
                if (view->get_transformer("alpha"))
                {
                    view->pop_transformer("alpha");
                }
            }
        }

        output->rem_binding(&axis_cb);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_alpha);
