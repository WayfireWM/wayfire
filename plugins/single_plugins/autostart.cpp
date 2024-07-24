#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/config/config-manager.hpp>
#include <config.h>

class wayfire_autostart : public wf::plugin_interface_t
{
    wf::option_wrapper_t<bool> autostart_wf_shell{"autostart/autostart_wf_shell"};
    wf::option_wrapper_t<wf::config::compound_list_t<std::string>>
    autostart_entries{"autostart/autostart"};

  public:
    void init() override
    {
        /* Run only once, at startup */
        auto section = wf::get_core().config->get_section("autostart");

        bool panel_manually_started = false;
        bool background_manually_started = false;

        for (const auto& [name, command] : autostart_entries.value())
        {
            // Because we accept any option names, we should ignore regular
            // options
            if (name == "autostart_wf_shell")
            {
                continue;
            }

            wf::get_core().run(command);
            if (command.find("wf-panel") != std::string::npos)
            {
                panel_manually_started = true;
            }

            if (command.find("wf-background") != std::string::npos)
            {
                background_manually_started = true;
            }
        }

        if (autostart_wf_shell && !panel_manually_started)
        {
            wf::get_core().run("wf-panel");
        }

        if (autostart_wf_shell && !background_manually_started)
        {
            wf::get_core().run("wf-background");
        }
    }

    bool is_unloadable() override
    {
        return false;
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_autostart);
