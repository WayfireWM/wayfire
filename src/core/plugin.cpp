#include "wayfire/debug.hpp"
#include <wayfire/util/log.hpp>
#include <wayfire/config-backend.hpp>
#include <wayfire/plugin.hpp>

void wf::plugin_interface_t::fini()
{}

namespace wf
{
/** Implementation of default config backend functions. */
std::shared_ptr<config::section_t> wf::config_backend_t::get_output_section(
    wlr_output *output)
{
    std::string name = output->name;
    name = "output:" + name;
    auto& config = wf::get_core().config;
    if (!config->get_section(name))
    {
        config->merge_section(
            config->get_section("output")->clone_with_name(name));
    }

    return config->get_section(name);
}

std::shared_ptr<config::section_t> wf::config_backend_t::get_input_device_section(
    wlr_input_device *device)
{
    std::string name = nonull(device->name);
    name = "input-device:" + name;
    auto& config = wf::get_core().config;
    if (!config->get_section(name))
    {
        config->merge_section(
            config->get_section("input-device")->clone_with_name(name));
    }

    return config->get_section(name);
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

    // also add XDG specific paths
    std::string xdg_data_dir;
    char *c_xdg_data_dir = std::getenv("XDG_DATA_HOME");
    char *c_user_home    = std::getenv("HOME");

    if (c_xdg_data_dir != NULL)
    {
        xdg_data_dir = c_xdg_data_dir;
    } else if (c_user_home != NULL)
    {
        xdg_data_dir = (std::string)c_user_home + "/.local/share/";
    }

    if (xdg_data_dir != "")
    {
        xmldirs.push_back(xdg_data_dir + "/wayfire/metadata");
    }

    xmldirs.push_back(PLUGIN_XML_DIR);
    return xmldirs;
}
}
