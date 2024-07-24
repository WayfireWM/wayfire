#include "wayfire/debug.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/config-backend.hpp>
#include <wayfire/plugin.hpp>
#include <libudev.h>
#include <wayfire/plugin.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

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

static struct udev_property_and_desc
{
    char const *property_name;
    char const *description;
} properties_and_descs[] =
{
    {"ID_PATH", "stable physical connection path"},
    {"ID_SERIAL", "stable vendor+pn+sn info"},
    {"LIBINPUT_DEVICE_GROUP", "stable libinput info"},
    // sometimes it contains info "by path", sometimes "by id"
    {"DEVPATH", "unstable devpath"},
    // used for debugging, to find DEVPATH and match the right
    // device in `udevadm info --tree`
};

std::shared_ptr<config::section_t> wf::config_backend_t::get_input_device_section(
    std::string const & prefix, wlr_input_device *device)
{
    auto& config = wf::get_core().config;
    std::shared_ptr<wf::config::section_t> section;

    if (wlr_input_device_is_libinput(device))
    {
        auto libinput_dev = wlr_libinput_get_device_handle(device);
        if (libinput_dev)
        {
            udev_device *udev_dev = libinput_device_get_udev_device(libinput_dev);
            if (udev_dev)
            {
                for (struct udev_property_and_desc const & pd : properties_and_descs)
                {
                    const char *value = udev_device_get_property_value(udev_dev, pd.property_name);
                    if (value == nullptr)
                    {
                        continue;
                    }

                    std::string name = prefix + ":" + nonull(value);
                    LOGC(INPUT_DEVICES, "Checking for config section [", name, "] ",
                        pd.property_name, " (", pd.description, ")");
                    section = config->get_section(name);
                    if (section)
                    {
                        LOGC(INPUT_DEVICES, "Using config section [", name, "] for ", nonull(device->name));
                        return section;
                    }
                }
            }
        }
    }

    std::string name = nonull(device->name);
    name = prefix + ":" + name;
    LOGC(INPUT_DEVICES, "Checking for config section [", name, "]");

    if (!config->get_section(name))
    {
        // For input-device:(*) section, we always use per-device sections.
        // For input:(*) sections, we fall back to the common [input] section.
        if (prefix == "input")
        {
            LOGC(INPUT_DEVICES, "Using default config section [", prefix, "]");
            section = config->get_section(prefix);
        } else
        {
            LOGC(INPUT_DEVICES, "Creating config section [", name, "]");
            section = config->get_section(prefix)->clone_with_name(name);
            config->merge_section(section);
        }
    } else
    {
        LOGC(INPUT_DEVICES, "Using config section [", name, "]");
        section = config->get_section(name);
    }

    return section;
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
