#include <wayfire/per-output-plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/touch/touch.hpp>
#include <wayfire/view.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/output.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>

#include <wayfire/util/log.hpp>

namespace wf
{
using namespace touch;
class extra_gestures_plugin_t : public per_output_plugin_instance_t
{
    gesture_t touch_and_hold_move;
    gesture_t tap_to_close;

    wf::option_wrapper_t<int> move_fingers{"extra-gestures/move_fingers"};
    wf::option_wrapper_t<int> move_delay{"extra-gestures/move_delay"};

    wf::option_wrapper_t<int> close_fingers{"extra-gestures/close_fingers"};

    wf::plugin_activation_data_t grab_interface = {
        .capabilities = CAPABILITY_MANAGE_COMPOSITOR,
    };

  public:
    void init() override
    {
        build_touch_and_hold_move();
        move_fingers.set_callback([=] () { build_touch_and_hold_move(); });
        move_delay.set_callback([=] () { build_touch_and_hold_move(); });

        build_tap_to_close();
        close_fingers.set_callback([=] () { build_tap_to_close(); });
    }

    /**
     * Run an action on the view under the touch points, if the touch points
     * are on the current output and the view is toplevel.
     */
    void execute_view_action(std::function<void(wayfire_view)> action)
    {
        auto& core = wf::get_core();
        auto state = core.get_touch_state();
        auto center_touch_point = state.get_center().current;
        wf::pointf_t center     = {center_touch_point.x, center_touch_point.y};

        if (core.output_layout->get_output_at(center.x, center.y) != this->output)
        {
            return;
        }

        /** Make sure we don't interfere with already activated plugins */
        if (!output->can_activate_plugin(&this->grab_interface))
        {
            return;
        }

        auto view = core.get_view_at({center.x, center.y});
        if (view && (view->role == VIEW_ROLE_TOPLEVEL))
        {
            action(view);
        }
    }

    void build_touch_and_hold_move()
    {
        wf::get_core().rem_touch_gesture(&touch_and_hold_move);

        touch_and_hold_move = wf::touch::gesture_builder_t()
            .action(touch_action_t(move_fingers, true)
            .set_move_tolerance(50)
            .set_duration(100))
            .action(hold_action_t(move_delay)
                .set_move_tolerance(100))
            .on_completed([=] ()
        {
            execute_view_action([] (wayfire_view view)
            {
                if (auto toplevel = wf::toplevel_cast(view))
                {
                    wf::get_core().default_wm->move_request(toplevel);
                }
            });
        }).build();

        wf::get_core().add_touch_gesture(&touch_and_hold_move);
    }

    void build_tap_to_close()
    {
        wf::get_core().rem_touch_gesture(&tap_to_close);

        tap_to_close = wf::touch::gesture_builder_t()
            .action(touch_action_t(close_fingers, true)
            .set_move_tolerance(50)
            .set_duration(150))
            .action(touch_action_t(close_fingers, false)
                .set_move_tolerance(50)
                .set_duration(150))
            .on_completed([=] () { execute_view_action([] (wayfire_view view) { view->close(); }); })
            .build();
        wf::get_core().add_touch_gesture(&tap_to_close);
    }

    void fini() override
    {
        wf::get_core().rem_touch_gesture(&touch_and_hold_move);
        wf::get_core().rem_touch_gesture(&tap_to_close);
    }
};
}

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf::extra_gestures_plugin_t>);
