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
class output_t;

/**
 * Plugins can set their capabilities to indicate what kind of plugin they are.
 * At any point, only one plugin with a given capability can be active on its
 * output (although multiple plugins with the same capability can be loaded).
 */
enum plugin_capabilities_t
{
    /** The plugin provides view decorations */
    CAPABILITY_VIEW_DECORATOR    = 1 << 0,
    /** The plugin grabs input.
     * Required in order to use plugin_grab_interface_t::grab() */
    CAPABILITY_GRAB_INPUT        = 1 << 1,
    /** The plugin uses custom renderer */
    CAPABILITY_CUSTOM_RENDERER   = 1 << 2,
    /** The plugin manages the whole desktop, for ex. switches workspaces. */
    CAPABILITY_MANAGE_DESKTOP    = 1 << 3,
    /* Compound capabilities */

    /** The plugin manages the whole compositor state */
    CAPABILITY_MANAGE_COMPOSITOR = CAPABILITY_GRAB_INPUT |
        CAPABILITY_MANAGE_DESKTOP | CAPABILITY_CUSTOM_RENDERER,
};

/**
 * The plugin grab interface is what the plugins use to announce themselves to
 * the core and other plugins as active, and to request to grab input.
 */
struct plugin_grab_interface_t
{
  private:
    bool grabbed = false;

  public:
    /** The name of the plugin. Not important */
    std::string name;
    /** The plugin capabilities. A bitmask of the values specified above */
    uint32_t capabilities;
    /** The output the grab interface is on */
    wf::output_t*const output;

    plugin_grab_interface_t(wf::output_t *_output);

    /**
     * Grab the input on the output. Requires CAPABILITY_GRAB_INPUT.
     * On successful grab, core will reset keyboard/pointer/touch focus.
     *
     * @return True if input was successfully grabbed.
     */
    bool grab();
    /** @return If the grab interface is grabbed */
    bool is_grabbed();
    /** Ungrab input, if it is grabbed. */
    void ungrab();

    /**
     * When grabbed, core will redirect all input events to the grabbing plugin.
     * The grabbing plugin can subscribe to different input events by setting
     * the callbacks below.
     */
    struct
    {
        struct
        {
            std::function<void(wlr_pointer_axis_event*)> axis;
            std::function<void(uint32_t, uint32_t)> button; // button, state
            std::function<void(int32_t, int32_t)> motion; // x, y
            std::function<void(wlr_pointer_motion_event*)> relative_motion;
        } pointer;

        struct
        {
            std::function<void(uint32_t, uint32_t)> key; // button, state
            std::function<void(uint32_t, uint32_t)> mod; // modifier, state
        } keyboard;

        struct
        {
            std::function<void(int32_t, int32_t, int32_t)> down; // id, x, y
            std::function<void(int32_t)> up; // id
            std::function<void(int32_t, int32_t, int32_t)> motion; // id, x, y
        } touch;

        /**
         * Each plugin might be deactivated forcefully, for example when the
         * desktop is locked. Plugins MUST honor this signal and exit their
         * grabs/renderers immediately.
         *
         * Note that cancel() is emitted even when the plugin is just activated
         * without grabbing input.
         */
        std::function<void()> cancel;
    } callbacks;
};

using plugin_grab_interface_uptr = std::unique_ptr<plugin_grab_interface_t>;

class plugin_interface_t
{
  public:
    /**
     * The output this plugin is running on. Initialized by core.
     * Each output has its own set of plugin instances. This way, a plugin
     * rarely if ever needs to care about multi-monitor setups.
     */
    wf::output_t *output;

    /**
     * The grab interface of the plugin, initialized by core.
     */
    std::unique_ptr<plugin_grab_interface_t> grab_interface;

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

    virtual ~plugin_interface_t();

    /** Handle to the plugin's .so file, used by the plugin loader */
    void *handle = NULL;
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
constexpr uint32_t WAYFIRE_API_ABI_VERSION = 2020'01'24;

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
