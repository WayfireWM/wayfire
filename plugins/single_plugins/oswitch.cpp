#include "wayfire/plugin.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/view-helpers.hpp"
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/seat.hpp>

class wayfire_oswitch : public wf::plugin_interface_t
{
    wf::wl_idle_call idle_switch_output;

    wf::output_t *get_output_relative(int step)
    {
        /* get the target output n steps after current output
         * if current output's index is i, and if there're n monitors
         * then return the (i + step) mod n th monitor */
        auto current_output = wf::get_core().seat->get_active_output();
        auto os = wf::get_core().output_layout->get_outputs();
        auto it = std::find(os.begin(), os.end(), current_output);
        if (it == os.end())
        {
            LOGI("Current output not found in output list");
            return current_output;
        }

        int size = os.size();
        int current_index = it - os.begin();
        int target_index  = ((current_index + step) % size + size) % size;
        return os[target_index];
    }

    void switch_to_output(wf::output_t *target_output)
    {
        /* when we switch the output, the oswitch keybinding
         * may be activated for the next output, which we don't want,
         * so we postpone the switch */
        idle_switch_output.run_once([=] ()
        {
            wf::get_core().seat->focus_output(target_output);
            target_output->ensure_pointer(true);
        });
    }

    void switch_to_output_with_window(wf::output_t *target_output)
    {
        auto current_output = wf::get_core().seat->get_active_output();
        auto view =
            wf::find_topmost_parent(wf::toplevel_cast(wf::get_active_view_for_output(current_output)));
        if (view)
        {
            move_view_to_output(view, target_output, true);
        }

        switch_to_output(target_output);
    }

    wf::activator_callback next_output = [=] (auto)
    {
        auto target_output = get_output_relative(1);
        switch_to_output(target_output);
        return true;
    };

    wf::activator_callback next_output_with_window = [=] (auto)
    {
        auto target_output = get_output_relative(1);
        switch_to_output_with_window(target_output);
        return true;
    };

    wf::activator_callback prev_output = [=] (auto)
    {
        auto target_output = get_output_relative(-1);
        switch_to_output(target_output);
        return true;
    };

    wf::activator_callback prev_output_with_window = [=] (auto)
    {
        auto target_output = get_output_relative(-1);
        switch_to_output_with_window(target_output);
        return true;
    };

  public:
    void init()
    {
        auto& bindings = wf::get_core().bindings;
        bindings->add_activator(wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/next_output"},
            &next_output);
        bindings->add_activator(wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/next_output_with_win"},
            &next_output_with_window);
        bindings->add_activator(wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/prev_output"},
            &prev_output);
        bindings->add_activator(wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/prev_output_with_win"},
            &prev_output_with_window);
    }

    void fini()
    {
        auto& bindings = wf::get_core().bindings;
        bindings->rem_binding(&next_output);
        bindings->rem_binding(&next_output_with_window);
        bindings->rem_binding(&prev_output);
        bindings->rem_binding(&prev_output_with_window);
        idle_switch_output.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_oswitch);
