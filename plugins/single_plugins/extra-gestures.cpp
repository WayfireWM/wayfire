#include <wayfire/per-output-plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/touch/touch.hpp>
#include <wayfire/view.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/output.hpp>
#include <wayfire/toplevel-view.hpp>

#include <wayfire/util/log.hpp>

namespace wf
{
using namespace touch;
class extra_gestures_plugin_t : public per_output_plugin_instance_t
{
    std::unique_ptr<gesture_t> touch_and_hold_move;
    std::unique_ptr<gesture_t> tap_to_close;

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
        wf::get_core().add_touch_gesture({touch_and_hold_move});

        build_tap_to_close();
        close_fingers.set_callback([=] () { build_tap_to_close(); });
        wf::get_core().add_touch_gesture({tap_to_close});
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
        if (touch_and_hold_move)
        {
            wf::get_core().rem_touch_gesture({touch_and_hold_move});
        }

        auto touch_down = std::make_unique<wf::touch_action_t>(move_fingers, true);
        touch_down->set_move_tolerance(50);
        touch_down->set_duration(100);

        auto hold = std::make_unique<wf::hold_action_t>(move_delay);
        hold->set_move_tolerance(100);

        std::vector<std::unique_ptr<gesture_action_t>> actions;
        actions.emplace_back(std::move(touch_down));
        actions.emplace_back(std::move(hold));
        touch_and_hold_move = std::make_unique<gesture_t>(std::move(actions),
            [=] ()
        {
            execute_view_action([] (wayfire_view view)
            {
                if (auto toplevel = wf::toplevel_cast(view))
                {
                    toplevel->move_request();
                }
            });
        });
    }

    void build_tap_to_close()
    {
        if (tap_to_close)
        {
            wf::get_core().rem_touch_gesture({tap_to_close});
        }

        auto touch_down = std::make_unique<wf::touch_action_t>(close_fingers, true);
        touch_down->set_move_tolerance(50);
        touch_down->set_duration(150);

        auto touch_up = std::make_unique<wf::touch_action_t>(close_fingers, false);
        touch_up->set_move_tolerance(50);
        touch_up->set_duration(150);

        std::vector<std::unique_ptr<gesture_action_t>> actions;
        actions.emplace_back(std::move(touch_down));
        actions.emplace_back(std::move(touch_up));
        tap_to_close = std::make_unique<gesture_t>(std::move(actions),
            [=] ()
        {
            execute_view_action([] (wayfire_view view) { view->close(); });
        });
    }

    void fini() override
    {
        wf::get_core().rem_touch_gesture({touch_and_hold_move});
        wf::get_core().rem_touch_gesture({tap_to_close});
    }
};
}

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf::extra_gestures_plugin_t>);
