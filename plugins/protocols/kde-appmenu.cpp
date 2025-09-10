#include "kde-appmenu-protocol.h"
#include "wayfire/core.hpp"
#include "wayfire/object.hpp"
#include <wayfire/plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include "kde-appmenu.hpp"

#define KDE_APPMENU_VERSION 2

struct wf_kde_appmenu_surface
{
    wl_resource *resource;
    wl_resource *wl_surface;
};

static void handle_kde_appmenu_set_address(wl_client *client,
    wl_resource *resource, const char *service_name,
    const char *object_path)
{
    auto surface = static_cast<wf_kde_appmenu_surface*>(wl_resource_get_user_data(resource));
    wayfire_view view = wf::wl_surface_to_wayfire_view(surface->wl_surface);
    if (!view)
    {
        LOGE("Could not get view");
        return;
    } else
    {
        kde_appmenu_dbus_address_signal ev;
        ev.view = view;
        ev.service_name = service_name;
        ev.object_path  = object_path;

        wf::get_core().emit(&ev);
    }
}

static void handle_kde_appmenu_release(wl_client*, wl_resource*)
{
    /* no-op */
}

static void handle_kde_appmenu_destroy(wl_resource *resource)
{
    auto surface = static_cast<wf_kde_appmenu_surface*>(wl_resource_get_user_data(resource));
    delete surface;
}

const struct org_kde_kwin_appmenu_interface kde_appmenu_impl = {
    .set_address = handle_kde_appmenu_set_address,
    .release     = handle_kde_appmenu_release
};


static void handle_kde_appmenu_manager_create(wl_client *client,
    wl_resource *resource, uint32_t id, wl_resource *surface)
{
    wf_kde_appmenu_surface *kde_appmenu_surface = new wf_kde_appmenu_surface;
    kde_appmenu_surface->resource = wl_resource_create(client,
        &org_kde_kwin_appmenu_interface, wl_resource_get_version(resource), id);
    kde_appmenu_surface->wl_surface = surface;
    wl_resource_set_implementation(kde_appmenu_surface->resource, &kde_appmenu_impl,
        kde_appmenu_surface, handle_kde_appmenu_destroy);
}

static void handle_kde_appmenu_manager_release(wl_client*, wl_resource*)
{
    /* no-op */
}

static void handle_kde_appmenu_manager_destroy(wl_resource*)
{
    /* no-op */
}

static const struct org_kde_kwin_appmenu_manager_interface kde_appmenu_manager_impl = {
    .create  = handle_kde_appmenu_manager_create,
    .release = handle_kde_appmenu_manager_release
};


static void bind_kde_appmenu(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    auto resource = wl_resource_create(client, &org_kde_kwin_appmenu_manager_interface,
        KDE_APPMENU_VERSION, id);
    wl_resource_set_implementation(resource, &kde_appmenu_manager_impl,
        data, handle_kde_appmenu_manager_destroy);
}

class wayfire_kde_appmenu_impl : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        auto display = wf::get_core().display;
        wl_global_create(display, &org_kde_kwin_appmenu_manager_interface,
            KDE_APPMENU_VERSION, NULL, bind_kde_appmenu);
    }

    bool is_unloadable() override
    {
        return false;
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_kde_appmenu_impl);
