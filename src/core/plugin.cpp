#include "core-impl.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/output.hpp"
#include "seat/input-manager.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/util/log.hpp>
#include <wayfire/config-backend.hpp>

void wf::plugin_interface_t::fini()
{}
wf::plugin_interface_t::~plugin_interface_t()
{}

namespace wf
{
wayfire_view get_signaled_view(wf::signal_data_t *data)
{
    auto conv = static_cast<wf::_view_signal*>(data);
    if (!conv)
    {
        LOGE("Got a bad _view_signal");

        return nullptr;
    }

    return conv->view;
}

wf::output_t *get_signaled_output(wf::signal_data_t *data)
{
    auto result = static_cast<wf::_output_signal*>(data);

    return result ? result->output : nullptr;
}

/** Implementation of default config backend functions. */
std::shared_ptr<config::section_t> wf::config_backend_t::get_output_section(
    wlr_output *output)
{
    std::string name = output->name;
    name = "output:" + name;
    auto& config = wf::get_core().config;
    if (!config.get_section(name))
    {
        config.merge_section(
            config.get_section("output")->clone_with_name(name));
    }

    return config.get_section(name);
}

std::shared_ptr<config::section_t> wf::config_backend_t::get_input_device_section(
    wlr_input_device *device)
{
    std::string name = nonull(device->name);
    name = "input-device:" + name;
    auto& config = wf::get_core().config;
    if (!config.get_section(name))
    {
        config.merge_section(
            config.get_section("input-device")->clone_with_name(name));
    }

    return config.get_section(name);
}

std::vector<std::string> wf::config_backend_t::get_xml_dirs() const
{
    std::vector<std::string> xmldirs;
    if (char *plugin_xml_path = getenv("WAYFIRE_PLUGIN_XML_PATH"))
    {
        std::stringstream ss(plugin_xml_path);
        std::string entry;
        while (std::getline(ss, entry, ':'))
        {
            xmldirs.push_back(entry);
        }
    }

    xmldirs.push_back(PLUGIN_XML_DIR);
    return xmldirs;
}
}
