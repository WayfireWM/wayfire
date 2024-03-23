#include <sstream>
#include <algorithm>
#include <memory>
#include <filesystem>
#include <dlfcn.h>

#include "plugin-loader.hpp"
#include "../core/wm.hpp"
#include "wayfire/plugin.hpp"
#include <wayfire/util/log.hpp>

wf::plugin_manager_t::plugin_manager_t()
{
    this->plugins_opt.load_option("core/plugins");
    this->enable_so_unloading.load_option("workarounds/enable_so_unloading");

    reload_dynamic_plugins();
    load_static_plugins();

    this->plugins_opt.set_callback([=] ()
    {
        /* reload when config reload has finished */
        idle_reload_plugins.run_once([&] () { reload_dynamic_plugins(); });
    });
}

void wf::plugin_manager_t::deinit_plugins(bool unloadable)
{
    for (auto& [name, plugin] : loaded_plugins)
    {
        if (!plugin.instance)
        {
            continue;
        }

        if (plugin.instance->is_unloadable() == unloadable)
        {
            destroy_plugin(plugin);
        }
    }
}

wf::plugin_manager_t::~plugin_manager_t()
{
    /* First remove unloadable plugins, then others */
    deinit_plugins(true);
    deinit_plugins(false);

    loaded_plugins.clear();
}

void wf::plugin_manager_t::destroy_plugin(wf::loaded_plugin_t& p)
{
    LOGD("Unloading plugin ", p.so_path);
    p.instance->fini();
    p.instance.reset();

    /* dlopen()/dlclose() do reference counting, so we should close the plugin
     * as many times as we opened it.
     *
     * We also need to close the handle after deallocating the plugin, otherwise
     * we unload its destructor before calling it.
     *
     * Note however, that dlclose() is merely a "statement of intent" as per
     * POSIX[1]:
     * - On glibc[2], this decreases the reference count and potentially unloads
     *   the binary.
     * - On musl-libc[3] this is a noop.
     *
     * [1]: https://pubs.opengroup.org/onlinepubs/9699919799/functions/dlclose.html
     * [2]: https://man7.org/linux/man-pages/man3/dlclose.3.html
     * [3]:
     * https://wiki.musl-libc.org/functional-differences-from-glibc.html#Unloading-libraries
     * */
    if (p.so_handle && enable_so_unloading)
    {
        dlclose(p.so_handle);
    }
}

std::pair<void*, void*> wf::get_new_instance_handle(const std::string& path)
{
    // RTLD_GLOBAL is required for RTTI/dynamic_cast across plugins
    void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (handle == NULL)
    {
        LOGE("error loading plugin: ", dlerror());
        return {nullptr, nullptr};
    }

    /* Check plugin version */
    auto version_func_ptr = dlsym(handle, "getWayfireVersion");
    if (version_func_ptr == NULL)
    {
        LOGE(path, ": missing getWayfireVersion()", path.c_str());
        dlclose(handle);
        return {nullptr, nullptr};
    }

    auto version_func = union_cast<void*, wayfire_plugin_version_func>(version_func_ptr);
    int32_t plugin_abi_version = version_func();

    if (version_func() != WAYFIRE_API_ABI_VERSION)
    {
        LOGE(path, ": API/ABI version mismatch: Wayfire is ",
            WAYFIRE_API_ABI_VERSION, ",  plugin built with ", plugin_abi_version);
        dlclose(handle);
        return {nullptr, nullptr};
    }

    auto new_instance_func_ptr = dlsym(handle, "newInstance");
    if (new_instance_func_ptr == NULL)
    {
        LOGE(path, ": missing newInstance(). ", dlerror());
        dlclose(handle);
        return {nullptr, nullptr};
    }

    LOGD("Loaded plugin ", path.c_str());

    return {handle, new_instance_func_ptr};
}

std::optional<wf::loaded_plugin_t> wf::plugin_manager_t::load_plugin_from_file(std::string path)
{
    auto [handle, new_instance_func_ptr] = wf::get_new_instance_handle(path);
    if (new_instance_func_ptr)
    {
        auto new_instance_func = union_cast<void*, wayfire_plugin_load_func>(new_instance_func_ptr);

        loaded_plugin_t lp;
        lp.instance  = std::unique_ptr<wf::plugin_interface_t>(new_instance_func());
        lp.so_handle = handle;
        lp.so_path   = path;
        return lp;
    }

    return {};
}

void wf::plugin_manager_t::reload_dynamic_plugins()
{
    std::string plugin_list = plugins_opt;
    if (plugin_list == "none")
    {
        LOGE("No plugins specified in the config file, or config file is "
             "missing. In this state the compositor is nearly unusable, please "
             "ensure your configuration file is set up properly.");
    }

    std::stringstream stream(plugin_list);
    std::vector<std::string> next_plugins;
    std::vector<std::string> plugin_paths = wf::get_plugin_paths();

    std::string plugin_name;
    while (stream >> plugin_name)
    {
        if (plugin_name.size())
        {
            auto plugin_path = wf::get_plugin_path_for_name(plugin_paths, plugin_name);
            if (plugin_path)
            {
                auto already_loaded =
                    std::find(next_plugins.begin(), next_plugins.end(), plugin_path.value());
                if (already_loaded != next_plugins.end())
                {
                    LOGE(plugin_name, " plugin found in the plugin list more than once, skipping");
                    continue;
                }

                next_plugins.push_back(plugin_path.value());
            } else
            {
                LOGE("Failed to load plugin \"", plugin_name, "\". ",
                    "Make sure it is installed in ", PLUGIN_PATH, " or in $WAYFIRE_PLUGIN_PATH.");
            }
        }
    }

    /* erase plugins that have been removed from the config */
    auto it = loaded_plugins.begin();
    while (it != loaded_plugins.end())
    {
        /* skip built-in(static) plugins */
        if (it->first.size() && (it->first[0] == '_'))
        {
            ++it;
            continue;
        }

        if ((std::find(next_plugins.begin(), next_plugins.end(), it->first) == next_plugins.end()) &&
            it->second.instance->is_unloadable())
        {
            LOGD("unload plugin ", it->first.c_str());
            destroy_plugin(it->second);
            it = loaded_plugins.erase(it);
        } else
        {
            ++it;
        }
    }

    /* load new plugins */
    std::vector<std::pair<std::string, wf::loaded_plugin_t>> pending_initialize;

    for (auto plugin : next_plugins)
    {
        if (loaded_plugins.count(plugin))
        {
            continue;
        }

        std::optional<wf::loaded_plugin_t> ptr = load_plugin_from_file(plugin);
        if (ptr)
        {
            pending_initialize.emplace_back(plugin, std::move(*ptr));
        }
    }

    std::stable_sort(pending_initialize.begin(), pending_initialize.end(), [] (const auto& a, const auto& b)
    {
        return a.second.instance->get_order_hint() < b.second.instance->get_order_hint();
    });

    for (auto& [plugin, ptr] : pending_initialize)
    {
        ptr.instance->init();
        loaded_plugins[plugin] = std::move(ptr);
    }
}

template<class T>
static wf::loaded_plugin_t create_plugin(std::string name)
{
    wf::loaded_plugin_t lp;
    lp.instance  = std::make_unique<T>();
    lp.so_handle = nullptr;
    lp.so_path   = name;
    lp.instance->init();
    return lp;
}

void wf::plugin_manager_t::load_static_plugins()
{
    loaded_plugins["_exit"]  = create_plugin<wf::per_output_plugin_t<wayfire_exit>>("_exit");
    loaded_plugins["_focus"] = create_plugin<wayfire_focus>("_focus");
    loaded_plugins["_close"] = create_plugin<wf::per_output_plugin_t<wayfire_close>>("_close");
}

std::vector<std::string> wf::get_plugin_paths()
{
    std::vector<std::string> plugin_prefixes;
    if (char *plugin_path = getenv("WAYFIRE_PLUGIN_PATH"))
    {
        std::stringstream ss(plugin_path);
        std::string entry;
        while (std::getline(ss, entry, ':'))
        {
            plugin_prefixes.push_back(entry);
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
        plugin_prefixes.push_back(xdg_data_dir + "/wayfire/plugins");
    }

    plugin_prefixes.push_back(PLUGIN_PATH);

    return plugin_prefixes;
}

std::optional<std::string> wf::get_plugin_path_for_name(
    std::vector<std::string> plugin_paths, std::string plugin_name)
{
    if (plugin_name.at(0) == '/')
    {
        return plugin_name;
    }

    for (std::filesystem::path plugin_prefix : plugin_paths)
    {
        auto plugin_path = plugin_prefix / ("lib" + plugin_name + ".so");
        if (std::filesystem::exists(plugin_path))
        {
            return plugin_path;
        }
    }

    return {};
}
