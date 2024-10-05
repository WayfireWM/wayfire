#include <wayfire/per-output-plugin.hpp>
#include "wayfire/debug.hpp"
#include "wayfire/plugins/common/input-grab.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/view.hpp"
#include "wayfire/view-transform.hpp"
#include "wayfire/output.hpp"
#include "wayfire/core.hpp"
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/util.hpp>
#include <wayfire/window-manager.hpp>
#include <linux/input.h>

#include <glm/gtc/matrix_transform.hpp>

static const char *transformer_3d = "wrot-3d";
static const char *transformer_2d = "wrot-2d";

static double cross(double x1, double y1, double x2, double y2) // cross product
{
    return x1 * y2 - x2 * y1;
}

static double vlen(double x1, double y1) // length of vector centered at the origin
{
    return std::sqrt(x1 * x1 + y1 * y1);
}

enum class mode
{
    NONE,
    ROT_2D,
    ROT_3D,
};

class wf_wrot : public wf::per_output_plugin_instance_t, public wf::pointer_interaction_t
{
    wf::button_callback call;
    wf::option_wrapper_t<double> reset_radius{"wrot/reset_radius"};
    wf::option_wrapper_t<int> sensitivity{"wrot/sensitivity"};
    wf::option_wrapper_t<bool> invert{"wrot/invert"};

    wf::pointf_t last_position;
    wayfire_toplevel_view current_view = nullptr;
    std::unique_ptr<wf::input_grab_t> input_grab;

    mode current_mode = mode::NONE;

    void reset_all()
    {
        for (auto v : wf::get_core().get_all_views())
        {
            v->get_transformed_node()->rem_transformer(transformer_2d);
            v->get_transformed_node()->rem_transformer(transformer_3d);
        }
    }

    wf::button_callback call_3d = [this] (auto)
    {
        if (current_mode != mode::NONE)
        {
            return false;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        current_view = toplevel_cast(wf::get_core().get_cursor_focus_view());
        if (!current_view || (current_view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            output->deactivate_plugin(&grab_interface);
            return false;
        }

        wf::get_core().default_wm->focus_raise_view(current_view);
        current_view->connect(&current_view_unmapped);
        input_grab->grab_input(wf::scene::layer::OVERLAY);

        last_position = output->get_cursor_position();
        current_mode  = mode::ROT_3D;
        return false; // pass btn press to grab node
    };

    wf::key_callback reset = [this] (auto)
    {
        reset_all();
        return true;
    };

    wf::key_callback reset_one = [this] (auto)
    {
        auto view = wf::get_active_view_for_output(output);
        if (view)
        {
            view->get_transformed_node()->rem_transformer(transformer_2d);
            view->get_transformed_node()->rem_transformer(transformer_3d);
        }

        return true;
    };

    wf::signal::connection_t<wf::view_unmapped_signal> current_view_unmapped = [this] (auto)
    {
        if (input_grab->is_grabbed())
        {
            current_view = nullptr;
            input_released();
        }
    };

    void motion_2d(int x, int y)
    {
        auto tr = wf::ensure_named_transformer<wf::scene::view_2d_transformer_t>(
            current_view, wf::TRANSFORMER_2D, transformer_2d, current_view);

        current_view->get_transformed_node()->begin_transform_update();
        auto g = current_view->get_geometry();

        double cx = g.x + g.width / 2.0;
        double cy = g.y + g.height / 2.0;

        double x1 = last_position.x - cx, y1 = last_position.y - cy;
        double x2 = x - cx, y2 = y - cy;

        if (vlen(x2, y2) <= reset_radius)
        {
            current_view->get_transformed_node()->end_transform_update();
            current_view->get_transformed_node()->rem_transformer(transformer_2d);
            return;
        }

        /* cross(a, b) = |a| * |b| * sin(a, b) */
        tr->angle -= std::asin(cross(x1, y1, x2, y2) / vlen(x1, y1) / vlen(x2,
            y2));

        current_view->get_transformed_node()->end_transform_update();
        last_position = {1.0 * x, 1.0 * y};
    }

    void motion_3d(int x, int y)
    {
        if ((x == last_position.x) && (y == last_position.y))
        {
            return;
        }

        auto tr = wf::ensure_named_transformer<wf::scene::view_3d_transformer_t>(
            current_view, wf::TRANSFORMER_3D, transformer_3d, current_view);

        current_view->get_transformed_node()->begin_transform_update();
        float dx     = x - last_position.x;
        float dy     = y - last_position.y;
        float ascale = glm::radians(sensitivity / 60.0f);
        float dir    = invert ? -1.f : 1.f;
        tr->rotation = glm::rotate<float>(tr->rotation, vlen(dx, dy) * ascale,
            {dir *dy, dir * dx, 0});
        current_view->get_transformed_node()->end_transform_update();
        last_position = {(double)x, (double)y};
    }

    wf::plugin_activation_data_t grab_interface = {
        .name = "wrot",
        .capabilities = wf::CAPABILITY_GRAB_INPUT,
    };

  public:
    void init() override
    {
        input_grab = std::make_unique<wf::input_grab_t>("wrot", output, nullptr, this, nullptr);
        call = [=] (auto)
        {
            if (current_mode != mode::NONE)
            {
                return false;
            }

            if (!output->activate_plugin(&grab_interface))
            {
                return false;
            }

            current_view = toplevel_cast(wf::get_core().get_cursor_focus_view());
            if (!current_view || (current_view->role != wf::VIEW_ROLE_TOPLEVEL))
            {
                output->deactivate_plugin(&grab_interface);
                return false;
            }

            wf::get_core().default_wm->focus_raise_view(current_view);
            current_view->connect(&current_view_unmapped);
            input_grab->grab_input(wf::scene::layer::OVERLAY);

            last_position = output->get_cursor_position();
            current_mode  = mode::ROT_2D;
            return false; // pass btn press to the grab node
        };

        output->add_button(wf::option_wrapper_t<wf::buttonbinding_t>("wrot/activate"), &call);
        output->add_button(wf::option_wrapper_t<wf::buttonbinding_t>("wrot/activate-3d"), &call_3d);
        output->add_key(wf::option_wrapper_t<wf::keybinding_t>{"wrot/reset"}, &reset);
        output->add_key(wf::option_wrapper_t<wf::keybinding_t>{"wrot/reset-one"}, &reset_one);

        grab_interface.cancel = [=] ()
        {
            if (input_grab->is_grabbed())
            {
                input_released();
            }
        };
    }

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        if (event.state == WL_POINTER_BUTTON_STATE_RELEASED)
        {
            input_released();
        }
    }

    void handle_pointer_motion(wf::pointf_t pointer_position, uint32_t time_ms) override
    {
        if (current_view && current_view->get_output())
        {
            auto og = current_view->get_output()->get_layout_geometry();
            pointer_position.x -= og.x;
            pointer_position.y -= og.y;
        }

        if (current_mode == mode::ROT_2D)
        {
            motion_2d(pointer_position.x, pointer_position.y);
        } else if (current_mode == mode::ROT_3D)
        {
            motion_3d(pointer_position.x, pointer_position.y);
        }
    }

    void input_released()
    {
        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);
        current_view_unmapped.disconnect();
        if ((current_mode == mode::ROT_3D) && current_view)
        {
            auto tr = current_view->get_transformed_node()
                ->get_transformer<wf::scene::view_3d_transformer_t>(transformer_3d);
            if (tr)
            {
                /* check if the view was rotated to perpendicular to the screen
                 * and move it a bit more, so it will not get "stuck" that way */
                glm::vec4 n{0, 0, 1.f, 0};
                glm::vec4 x = tr->rotation * n;
                auto dot    = glm::dot(n, x);
                if (std::abs(dot) < 0.05)
                {
                    /* rotate 2.5 degrees around an axis perpendicular to x */
                    current_view->get_transformed_node()->begin_transform_update();
                    tr->rotation = glm::rotate<float>(tr->rotation, glm::radians(
                        dot < 0 ? -2.5f : 2.5f), {x.y, -x.x, 0});
                    current_view->get_transformed_node()->end_transform_update();
                }
            }
        }

        current_mode = mode::NONE;
    }

    void fini() override
    {
        if (input_grab->is_grabbed())
        {
            input_released();
        }

        reset_all();

        output->rem_binding(&call);
        output->rem_binding(&call_3d);
        output->rem_binding(&reset);
        output->rem_binding(&reset_one);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf_wrot>);
