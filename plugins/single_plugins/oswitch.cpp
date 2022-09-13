#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output-layout.hpp>

class wayfire_output_manager : public wf::plugin_interface_t
{
    wf::wl_idle_call idle_next_output;

    wf::activator_callback switch_output = [=] (auto)
    {
        /* when we switch the output, the oswitch keybinding
         * may be activated for the next output, which we don't want,
         * so we postpone the switch */
        auto next =
            wf::get_core().output_layout->get_next_output(output);
        idle_next_output.run_once([=] ()
        {
            wf::get_core().focus_output(next);
        });

        return true;
    };


    wf::activator_callback switch_output_with_window = [=] (auto)
    {
        auto next =
            wf::get_core().output_layout->get_next_output(output);
        auto view = output->get_active_view();

        if (!view)
        {
            switch_output(wf::activator_data_t{});

            return true;
        }

        wf::get_core().move_view_to_output(view, next, true);
        idle_next_output.run_once([=] ()
        {
            wf::get_core().focus_output(next);
        });

        return true;
    };

    wf::wl_idle_call idle_previous_output;

    wf::activator_callback switch_output_previous = [=] (auto)
    {
        /* when we switch the output, the oswitch keybinding
         * may be activated for the previous output, which we don't want,
         * so we postpone the switch */
        auto previous =
            wf::get_core().output_layout->get_previous_output(output);
        idle_previous_output.run_once([=] ()
        {
            wf::get_core().focus_output(previous);
        });

        return true;
    };


    wf::activator_callback switch_output_with_window_previous = [=] (auto)
    {
        auto previous =
            wf::get_core().output_layout->get_previous_output(output);
        auto view = output->get_active_view();

        if (!view)
        {
            switch_output_previous(wf::activator_data_t{});

            return true;
        }

        wf::get_core().move_view_to_output(view, previous, true);
        idle_previous_output.run_once([=] ()
        {
            wf::get_core().focus_output(previous);
        });

        return true;
    };

  public:
    void init()
    {
        grab_interface->name = "oswitch";
        grab_interface->capabilities = 0;

        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/next_output"},
            &switch_output);
        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/next_output_with_win"},
            &switch_output_with_window);
        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/previous_output"},
            &switch_output_previous);
        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/previous_output_with_win"},
            &switch_output_with_window_previous);
    }

    void fini()
    {
        output->rem_binding(&switch_output);
        output->rem_binding(&switch_output_with_window);
        output->rem_binding(&switch_output_previous);
        output->rem_binding(&switch_output_with_window_previous);
        idle_next_output.disconnect();
        idle_previous_output.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_output_manager);
