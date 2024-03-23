#pragma once

#include <vector>
#include <unordered_map>
#include "wayfire/plugin.hpp"
#include "config.h"
#include "wayfire/util.hpp"
#include <wayfire/option-wrapper.hpp>

namespace wf
{
struct loaded_plugin_t
{
    // A pointer to the plugin
    std::unique_ptr<wf::plugin_interface_t> instance;

    // A handle returned by dlopen().
    void *so_handle;

    // A path to the .so file of the plugin.
    std::string so_path;
};

struct plugin_manager_t
{
    plugin_manager_t();
    ~plugin_manager_t();

    void reload_dynamic_plugins();
    wf::wl_idle_call idle_reload_plugins;

  private:
    wf::option_wrapper_t<std::string> plugins_opt;
    wf::option_wrapper_t<bool> enable_so_unloading;
    std::unordered_map<std::string, loaded_plugin_t> loaded_plugins;

    void deinit_plugins(bool unloadable);

    std::optional<loaded_plugin_t> load_plugin_from_file(std::string path);
    void load_static_plugins();
    void destroy_plugin(loaded_plugin_t& plugin);
};

/** Helper functions */
template<class A, class B>
B union_cast(A object)
{
    union
    {
        A x;
        B y;
    } helper;
    helper.x = object;
    return helper.y;
}

/**
 * Open a plugin file and check the file for version errors.
 *
 * On success, return the handle from dlopen() and the pointer to the
 * newInstance of the plugin.
 *
 * @return (dlopen() handle, newInstance pointer)
 */
std::pair<void*, void*> get_new_instance_handle(const std::string& path);

/**
 * List the locations where wayfire's plugins are installed.
 * This function takes care of env variable WAYFIRE_PLUGIN_PATH,
 * as well as the default location.
 */
std::vector<std::string> get_plugin_paths();

/**
 * Search each path specified in @param plugin_paths for a plugin named @param
 * plugin_name
 * @param plugin_paths A list of locations where wayfire plugins are installed
 * @param plugin_name The plugin to be searched. If @param plugin_name is an
 *   absolute path, then it is returned without modification.
 */
std::optional<std::string> get_plugin_path_for_name(
    std::vector<std::string> plugin_paths, std::string plugin_name);
}
