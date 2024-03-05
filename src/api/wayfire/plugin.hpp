#ifndef PLUGIN_H
#define PLUGIN_H

#include <functional>
#include <memory>
#include "wayfire/util.hpp"
#include "wayfire/bindings.hpp"

#include <wayfire/nonstd/wlroots.hpp>

class wayfire_config;
namespace wf
{
/**
 * Plugins can set their capabilities to indicate what kind of plugin they are.
 * At any point, only one plugin with a given capability can be active on its
 * output (although multiple plugins with the same capability can be loaded).
 */
enum plugin_capabilities_t
{
    /** The plugin grabs input.
     * Required in order to use plugin_grab_interface_t::grab() */
    CAPABILITY_GRAB_INPUT        = 1 << 0,
    /** The plugin uses custom renderer */
    CAPABILITY_CUSTOM_RENDERER   = 1 << 1,
    /** The plugin manages the whole desktop, for ex. switches workspaces. */
    CAPABILITY_MANAGE_DESKTOP    = 1 << 2,
    /* Compound capabilities */

    /** The plugin manages the whole compositor state */
    CAPABILITY_MANAGE_COMPOSITOR = CAPABILITY_GRAB_INPUT |
        CAPABILITY_MANAGE_DESKTOP | CAPABILITY_CUSTOM_RENDERER,
};

/**
 * Plugins use the plugin activation data to indicate that they are active on a particular output.
 * The information is used to avoid conflicts between plugins with the same capabilities.
 */
struct plugin_activation_data_t
{
    // The name of the plugin. Used mostly for debugging purposes.
    std::string name = "";
    // The plugin capabilities. A bitmask of the values specified above
    uint32_t capabilities = 0;

    /**
     * Each plugin might be deactivated forcefully, for example when the desktop is locked. Plugins should
     * honor this signal and exit their grabs/renderers immediately. Note: this is sent only to active
     * plugins.
     */
    std::function<void()> cancel = [] () {};
};

class plugin_interface_t
{
  public:
    /**
     * The init method is the entry of the plugin. In the init() method, the
     * plugin should register all bindings it provides, connect to signals, etc.
     */
    virtual void init() = 0;

    /**
     * The fini method is called when a plugin is unloaded. It should clean up
     * all global state it has set (for ex. signal callbacks, bindings, ...),
     * because the plugin will be freed after this.
     */
    virtual void fini();

    /**
     * A plugin can request that it is never unloaded, even if it is removed
     * from the config's plugin list.
     *
     * Note that unloading a plugin is sometimes unavoidable, for ex. when the
     * output the plugin is running on is destroyed. So non-unloadable plugins
     * should still provide proper fini() methods.
     */
    virtual bool is_unloadable()
    {
        return true;
    }

    /**
     * When Wayfire starts, plugins are first sorted according to their order_hint before being initialized.
     *
     * The initialization order can be important for plugins which provide basic services like IPC and should
     * therefore be loaded and initialized first.
     *
     * The lower the order_hint, the earlier the plugin will be loaded.
     * Plugins with equal order hints will be loaded according to the order in the `core/plugins` option.
     */
    virtual int get_order_hint() const
    {
        return 0;
    }

    virtual ~plugin_interface_t() = default;
};
}

/**
 * Each plugin must provide a function which instantiates the plugin's class
 * and returns the instance.
 *
 * This function must have the name newInstance() and should be declared with
 * extern "C" so that the loader can find it.
 */
using wayfire_plugin_load_func = wf::plugin_interface_t * (*)();

/** The version of Wayfire's API/ABI */
constexpr uint32_t WAYFIRE_API_ABI_VERSION = 2024'03'05;

/**
 * Each plugin must also provide a function which returns the Wayfire API/ABI
 * that it was compiled with.
 *
 * This function must have the name getWayfireVersion() and should be declared
 * with extern "C" so that the loader can find it.
 */
using wayfire_plugin_version_func = uint32_t (*)();

/**
 * A macro to declare the necessary functions, given the plugin class name
 */
#define DECLARE_WAYFIRE_PLUGIN(PluginClass) \
    extern "C" \
    { \
        wf::plugin_interface_t*newInstance() { return new PluginClass; } \
        uint32_t getWayfireVersion() { return WAYFIRE_API_ABI_VERSION; } \
    }

#endif
