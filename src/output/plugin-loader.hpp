#ifndef PLUGIN_LOADER_HPP
#define PLUGIN_LOADER_HPP

#include <vector>
#include <unordered_map>
#include "wayfire/plugin.hpp"
#include "config.h"
#include "wayfire/util.hpp"
#include <wayfire/option-wrapper.hpp>

namespace wf
{
class output_t;
}

class wayfire_config;

using wayfire_plugin = std::unique_ptr<wf::plugin_interface_t>;
struct plugin_manager
{
    plugin_manager(wf::output_t *o);
    ~plugin_manager();

    void reload_dynamic_plugins();
    wf::wl_idle_call idle_reaload_plugins;

  private:
    wf::output_t *output;
    wf::option_wrapper_t<std::string> plugins_opt;
    std::unordered_map<std::string, wayfire_plugin> loaded_plugins;

    void deinit_plugins(bool unloadable);

    wayfire_plugin load_plugin_from_file(std::string path);
    void load_static_plugins();

    void init_plugin(wayfire_plugin& plugin);
    void destroy_plugin(wayfire_plugin& plugin);
};

namespace wf
{
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
 *   absolute path, then it is retuned without modifiction.
 */
std::optional<std::string> get_plugin_path_for_name(
    std::vector<std::string> plugin_paths,
    std::string plugin_name);
}

#endif /* end of include guard: PLUGIN_LOADER_HPP */
