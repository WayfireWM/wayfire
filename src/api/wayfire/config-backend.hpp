#pragma once

#include <wayfire/config/config-manager.hpp>
#include <wayland-server-core.h>
#include <wayfire/nonstd/wlroots.hpp>

namespace wf
{
/**
 * A base class for configuration backend plugins.
 *
 * A configuration backend plugin is loaded immediately after creating a
 * wayland display and initializing the logging infrastructure. Because of
 * this, the configuration backend plugins are not allowed to use any
 * Wayfire APIs, but can use wf-config.
 *
 * The job of a configuration backend plugin is to populate and update the
 * configuration options used in the rest of the code.
 */
class config_backend_t
{
  public:
    /**
     * Initialize the config backend and do the initial loading of config options.
     * The config backend must follow the same option types as described in the XML
     * files.
     *
     * @param display The wayland display used by Wayfire.
     * @param config  A reference to the config manager which needs to be
     *   populated.
     * @param cmd_config_file The configuration file specified on the command line.
     */
    virtual void init(wl_display *display, config::config_manager_t& config,
        const std::string& cmd_config_file) = 0;

    /**
     * Find the output section for a given output.
     *
     * The returned section must be a valid output object as
     * described in output.xml
     */
    virtual std::shared_ptr<config::section_t> get_output_section(
        wlr_output *output);

    /**
     * Find the output section for a given input device.
     *
     * The returned section must be a valid config object as described
     * in <@prefix>.xml (typically input.xml or input-device.xml).
     */
    virtual std::shared_ptr<config::section_t> get_input_device_section(
        std::string const & prefix, wlr_input_device *device);

    virtual ~config_backend_t() = default;

  protected:
    /** A helper to read the XML directories that Wayfire looks at */
    virtual std::vector<std::string> get_xml_dirs() const;
};
}

/**
 * A macro to declare the necessary functions, given the backend class name.
 */
#define DECLARE_WAYFIRE_CONFIG_BACKEND(PluginClass) \
    extern "C" \
    { \
        wf::config_backend_t*newInstance() { return new PluginClass; } \
        uint32_t getWayfireVersion() { return WAYFIRE_API_ABI_VERSION; } \
    }
