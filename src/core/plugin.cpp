#include "core.hpp"
#include "output.hpp"
#include "seat/input-manager.hpp"
#include "signal-definitions.hpp"
#include "debug.hpp"
#include <cmath>

bool wayfire_grab_interface_t::grab()
{
    if (!(abilities_mask & WF_ABILITY_GRAB_INPUT))
    {
        log_error ("attempt to grab iface %s without input grabbing ability", name.c_str());
        return false;
    }

    if (grabbed)
        return true;

    if (!output->is_plugin_active(name))
        return false;

    grabbed = true;

    if (output == core->get_active_output())
        return core->input->grab_input(this);
    else
        return true;
}

void wayfire_grab_interface_t::ungrab()
{
    if (!grabbed)
        return;

    grabbed = false;
    if (output == core->get_active_output())
        core->input->ungrab_input();
}

bool wayfire_grab_interface_t::is_grabbed()
{
    return grabbed;
}

void wayfire_plugin_t::fini() {}
wayfire_plugin_t::~wayfire_plugin_t() {}

wayfire_view get_signaled_view(signal_data *data)
{
    auto conv = static_cast<_view_signal*> (data);
    if (!conv)
    {
        log_error("Got a bad _view_signal");
        return nullptr;
    }

    return conv->view;
}

bool get_signaled_state(signal_data *data)
{
    auto conv = static_cast<_view_state_signal*> (data);

    if (!conv || !conv->view)
    {
        log_error ("Got a bad _view_state_signal");
        return false;
    }

    return conv->state;
}

wayfire_output *get_signaled_output(signal_data *data)
{
    auto result = static_cast<_output_signal*> (data);
    return result ? result->output : nullptr;
}

