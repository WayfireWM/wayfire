#include <output.hpp>
#include "../../shared/config.hpp"
#include <core.hpp>

class wayfire_autostart : public wayfire_plugin_t
{
    public:
    void init(wayfire_config *config)
    {
        /* make sure we are run only when adding the first output */
        if (core->get_next_output(output) != output)
            return;

        auto section = config->get_section("autostart");

        for (const auto& command : section->options)
            core->run(command.second.c_str());
    }
};

extern "C"
{
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_autostart();
    }
}
