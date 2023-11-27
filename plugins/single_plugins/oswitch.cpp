#include "wayfire/plugin.hpp"
#include "wayfire/toplevel-view.hpp"
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/seat.hpp>

class wayfire_oswitch : public wf::plugin_interface_t
{
    wf::wl_idle_call idle_next_output;

    wf::activator_callback switch_output = [=] (auto)
    {
        /* when we switch the output, the oswitch keybinding
         * may be activated for the next output, which we don't want,
         * so we postpone the switch */
        auto current_output = wf::get_core().seat->get_active_output();
        auto next = wf::get_core().output_layout->get_next_output(current_output);
        idle_next_output.run_once([=] ()
        {
            wf::get_core().seat->focus_output(next);
            next->ensure_pointer(true);
        });

        return true;
    };

    wf::activator_callback switch_output_with_window = [=] (auto)
    {
        auto current_output = wf::get_core().seat->get_active_output();
        auto next = wf::get_core().output_layout->get_next_output(current_output);
        auto view = wf::toplevel_cast(wf::get_active_view_for_output(current_output));
        LOGI("Found view ", view);
        if (!view)
        {
            switch_output(wf::activator_data_t{});

            return true;
        }

        move_view_to_output(view, next, true);
        idle_next_output.run_once([=] ()
        {
            wf::get_core().seat->focus_output(next);
            next->ensure_pointer(true);
        });

        return true;
    };

  public:
    void init()
    {
        auto& bindings = wf::get_core().bindings;
        bindings->add_activator(wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/next_output"},
            &switch_output);
        bindings->add_activator(wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/next_output_with_win"},
            &switch_output_with_window);
    }

    void fini()
    {
        auto& bindings = wf::get_core().bindings;
        bindings->rem_binding(&switch_output);
        bindings->rem_binding(&switch_output_with_window);
        idle_next_output.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_oswitch);
