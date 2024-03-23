#pragma once

#include "wayfire/bindings-repository.hpp"
#include "hotspot-manager.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/debug.hpp>

struct wf::bindings_repository_t::impl
{
    /**
     * Recreate hotspots.
     *
     * The action will take place on the next idle.
     */
    void recreate_hotspots()
    {
        this->idle_recreate_hotspots.run_once([=] ()
        {
            if (enabled > 0)
            {
                hotspot_mgr.update_hotspots(activators);
            } else
            {
                hotspot_mgr.update_hotspots({});
            }
        });
    }

    void reparse_extensions();

    binding_container_t<wf::keybinding_t, key_callback> keys;
    binding_container_t<wf::keybinding_t, axis_callback> axes;
    binding_container_t<wf::buttonbinding_t, button_callback> buttons;
    binding_container_t<wf::activatorbinding_t, activator_callback> activators;

    hotspot_manager_t hotspot_mgr;

    wf::signal::connection_t<wf::reload_config_signal> on_config_reload = [=] (wf::reload_config_signal *ev)
    {
        recreate_hotspots();
        reparse_extensions();
    };

    wf::wl_idle_call idle_recreate_hotspots;
    wf::wl_idle_call idle_reparse_bindings;

    int enabled = 1;
};
