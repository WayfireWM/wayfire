#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/plugin.hpp"
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <chrono>

namespace wf
{
namespace preserve_output
{
static std::string make_output_identifier(wf::output_t *output)
{
    std::string identifier = "";
    identifier += nonull(output->handle->make);
    identifier += "|";
    identifier += nonull(output->handle->model);
    identifier += "|";
    identifier += nonull(output->handle->serial);
    return identifier;
}

struct per_output_state_t
{
    std::shared_ptr<wf::workspace_set_t> workspace_set;
    std::chrono::time_point<std::chrono::steady_clock> destroy_timestamp;
    bool was_focused = false;
};

class preserve_output_t : public wf::plugin_interface_t
{
    wf::option_wrapper_t<int> last_output_focus_timeout{"preserve-output/last_output_focus_timeout"};
    std::map<std::string, per_output_state_t> saved_outputs;

    bool focused_output_expired(const per_output_state_t& state) const
    {
        using namespace std::chrono;
        const auto now = steady_clock::now();
        const auto elapsed_since_focus = duration_cast<milliseconds>(now - state.destroy_timestamp).count();
        return elapsed_since_focus > last_output_focus_timeout;
    }

    void save_output(wf::output_t *output)
    {
        auto ident = make_output_identifier(output);
        auto& data = saved_outputs[ident];

        data.was_focused = (output == wf::get_core().seat->get_active_output());
        data.destroy_timestamp = std::chrono::steady_clock::now();
        data.workspace_set     = output->wset();

        LOGD("Saving workspace set ", data.workspace_set->get_index(), " from output ", output->to_string(),
            " with identifier ", ident);

        // Set a dummy workspace set with no views at all.
        output->set_workspace_set(wf::workspace_set_t::create());

        // Detach workspace set from its old output
        data.workspace_set->attach_to_output(nullptr);
    }

    void try_restore_output(wf::output_t *output)
    {
        std::string ident = make_output_identifier(output);
        if (!saved_outputs.count(ident))
        {
            LOGD("No saved identifier for ", output->to_string());
            return;
        }

        auto& data = saved_outputs[ident];

        auto new_output = data.workspace_set->get_attached_output();
        if (new_output && (new_output->wset() == data.workspace_set))
        {
            // The wset was moved to a different output => We should leave it where it is
            LOGD("Saved workspace for ", output->to_string(), " has been remapped to another output.");
            return;
        }

        LOGD("Restoring workspace set ", data.workspace_set->get_index(), " to output ", output->to_string());
        output->set_workspace_set(data.workspace_set);
        if (data.was_focused && !focused_output_expired(data))
        {
            wf::get_core().seat->focus_output(output);
        }

        saved_outputs.erase(ident);
    }

    wf::signal::connection_t<output_pre_remove_signal> output_pre_remove = [=] (output_pre_remove_signal *ev)
    {
        if (wlr_output_is_headless(ev->output->handle))
        {
            // For example, NOOP-1
            return;
        }

        if (wf::get_core().get_current_state() == compositor_state_t::RUNNING)
        {
            LOGD("Received pre-remove event: ", ev->output->to_string());
            save_output(ev->output);
        }
    };

    wf::signal::connection_t<output_added_signal> on_new_output = [=] (output_added_signal *ev)
    {
        if (wlr_output_is_headless(ev->output->handle))
        {
            // For example, NOOP-1
            return;
        }

        try_restore_output(ev->output);
    };

  public:
    void init() override
    {
        wf::get_core().output_layout->connect(&on_new_output);
        wf::get_core().output_layout->connect(&output_pre_remove);
    }
};
}
}

DECLARE_WAYFIRE_PLUGIN(wf::preserve_output::preserve_output_t);
