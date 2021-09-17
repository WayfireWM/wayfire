#include "gtk-shell.hpp"
#include "gtk-shell-protocol.h"
#include <wayfire/util/log.hpp>
#include <wayfire/view.hpp>
#include "../core/core-impl.hpp"
#include <wayfire/core.hpp>
#include <map>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/gtk-shell.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#define GTK_SHELL_VERSION 3

struct wf_gtk_shell
{
    std::map<wl_resource*, std::string> surface_app_id;
};

struct wf_gtk_surface
{
    wl_resource *resource;
    wl_resource *wl_surface;
    wf::wl_listener_wrapper on_configure;
    wf::wl_listener_wrapper on_destroy;
};

/**
 *  In gnome-shell/mutter/meta windows/views keep track of the properties
 *  specified as arguments here.
 *  Currently only the app_id is implemented / required.
 */
static void handle_gtk_surface_set_dbus_properties(wl_client *client,
    wl_resource *resource,
    const char *application_id, const char *app_menu_path, const char *menubar_path,
    const char *window_object_path, const char *application_object_path,
    const char *unique_bus_name)
{
    auto surface = static_cast<wf_gtk_surface*>(wl_resource_get_user_data(resource));
    if (application_id)
    {
        wf::get_core_impl().gtk_shell->surface_app_id[surface->wl_surface] =
            application_id;
    }
}

/**
 * Modal dialogs may be handled differently than non-modal dialogs.
 * It is a hint that this should be attached to the parent surface.
 * In gnome this does not affect input-focus.
 * This function sets the modal hint.
 */
static void handle_gtk_surface_set_modal(wl_client *client, wl_resource *resource)
{
    auto surface = static_cast<wf_gtk_surface*>(wl_resource_get_user_data(resource));
    wayfire_view view = wf::wl_surface_to_wayfire_view(surface->wl_surface);
    if (view)
    {
        view->store_data(std::make_unique<wf::custom_data_t>(), "gtk-shell-modal");
    }
}

/**
 * Modal dialogs may be handled differently than non-modal dialogs.
 * It is a hint that this should be attached to the parent surface.
 * In gnome this does not affect input-focus.
 * This function removes the modal hint.
 */
static void handle_gtk_surface_unset_modal(wl_client *client, wl_resource *resource)
{
    auto surface = static_cast<wf_gtk_surface*>(wl_resource_get_user_data(resource));
    wayfire_view view = wf::wl_surface_to_wayfire_view(surface->wl_surface);
    if (view)
    {
        view->erase_data("gtk-shell-modal");
    }
}

/**
 * The surface requests focus, for example single instance applications like
 * gnome-control-center, gnome-clocks, dconf-editor are single instance and if
 * they are already running and launched again, this will request that they get
 * focused.
 * This function is superseded by handle_gtk_surface_request_focus a newer
 * equivalelent
 * used by gtk-applications now. This function is for compatibility reasons.
 */
static void handle_gtk_surface_present(wl_client *client, wl_resource *resource,
    uint32_t time)
{
    auto surface = static_cast<wf_gtk_surface*>(wl_resource_get_user_data(resource));
    wayfire_view view = wf::wl_surface_to_wayfire_view(surface->wl_surface);
    if (view)
    {
        wf::view_focus_request_signal data;
        data.view = view;
        data.self_request = true;
        view->emit_signal("view-focus-request", &data);
        wf::get_core().emit_signal("view-focus-request", &data);
    }
}

/**
 * The surface requests focus, for example single instance applications like
 * gnome-control-center, gnome-clocks, dconf-editor are single instance and if
 * they are already running and launched again, this will request that they get
 * focused.
 */
static void handle_gtk_surface_request_focus(struct wl_client *client,
    struct wl_resource *resource,
    const char *startup_id)
{
    auto surface = static_cast<wf_gtk_surface*>(wl_resource_get_user_data(resource));
    wayfire_view view = wf::wl_surface_to_wayfire_view(surface->wl_surface);
    if (view)
    {
        wf::view_focus_request_signal data;
        data.view = view;
        data.self_request = true;
        view->emit_signal("view-focus-request", &data);
        wf::get_core().emit_signal("view-focus-request", &data);
    }
}

/**
 * Helper function used by send_gtk_surface_configure
 * and send_gtk_surface_configure_edges
 */
static void append_to_array(wl_array *array, uint32_t value)
{
    uint32_t *tmp;
    tmp  = (uint32_t*)wl_array_add(array, sizeof(*tmp));
    *tmp = value;
}

/**
 * Tells the client about the window state in more detail than xdg_surface.
 * This currently only includes which edges are tiled.
 */
static void send_gtk_surface_configure(wf_gtk_surface *surface, wayfire_view view)
{
    int version = wl_resource_get_version(surface->resource);
    wl_array states;
    wl_array_init(&states);

    if (view->tiled_edges)
    {
        append_to_array(&states, GTK_SURFACE1_STATE_TILED);
    }

    if ((version >= GTK_SURFACE1_STATE_TILED_TOP_SINCE_VERSION) &&
        (view->tiled_edges & WLR_EDGE_TOP))
    {
        append_to_array(&states, GTK_SURFACE1_STATE_TILED_TOP);
    }

    if ((version >= GTK_SURFACE1_STATE_TILED_RIGHT_SINCE_VERSION) &&
        (view->tiled_edges & WLR_EDGE_RIGHT))
    {
        append_to_array(&states, GTK_SURFACE1_STATE_TILED_RIGHT);
    }

    if ((version >= GTK_SURFACE1_STATE_TILED_BOTTOM_SINCE_VERSION) &&
        (view->tiled_edges & WLR_EDGE_BOTTOM))
    {
        append_to_array(&states, GTK_SURFACE1_STATE_TILED_BOTTOM);
    }

    if ((version >= GTK_SURFACE1_STATE_TILED_LEFT_SINCE_VERSION) &&
        (view->tiled_edges & WLR_EDGE_LEFT))
    {
        append_to_array(&states, GTK_SURFACE1_STATE_TILED_LEFT);
    }

    gtk_surface1_send_configure(surface->resource, &states);
    wl_array_release(&states);
}

/**
 * Tells gtk which edges should be resizable.
 */
static void send_gtk_surface_configure_edges(wf_gtk_surface *surface,
    wayfire_view view)
{
    wl_array edges;
    wl_array_init(&edges);

    if (!view->tiled_edges)
    {
        append_to_array(&edges, GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_TOP);
        append_to_array(&edges, GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_RIGHT);
        append_to_array(&edges, GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_BOTTOM);
        append_to_array(&edges, GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_LEFT);
    }

    gtk_surface1_send_configure_edges(surface->resource, &edges);
    wl_array_release(&edges);
}

/**
 * Augments xdg_surface's configure with additional gtk-specific information.
 */
static void handle_xdg_surface_on_configure(wf_gtk_surface *surface)
{
    wayfire_view view = wf::wl_surface_to_wayfire_view(surface->wl_surface);
    if (view)
    {
        send_gtk_surface_configure(surface, view);
        if (wl_resource_get_version(surface->resource) >=
            GTK_SURFACE1_CONFIGURE_EDGES_SINCE_VERSION)
        {
            send_gtk_surface_configure_edges(surface, view);
        }
    }
}

/**
 * Prevents a race condition where the xdg_surface is destroyed before
 * the gtk_surface's resource and the gtk_surface's destructor tries to
 * disconnect these signals which causes a use-after-free
 */
static void handle_xdg_surface_on_destroy(wf_gtk_surface *surface)
{
    surface->on_configure.disconnect();
    surface->on_destroy.disconnect();
}

/**
 * Destroys the gtk_surface object.
 */
static void handle_gtk_surface_destroy(wl_resource *resource)
{
    auto surface = static_cast<wf_gtk_surface*>(wl_resource_get_user_data(resource));
    delete surface;
}

/**
 * Supported functions of the gtk_surface_interface implementation
 */
const struct gtk_surface1_interface gtk_surface1_impl = {
    .set_dbus_properties = handle_gtk_surface_set_dbus_properties,
    .set_modal   = handle_gtk_surface_set_modal,
    .unset_modal = handle_gtk_surface_unset_modal,
    .present     = handle_gtk_surface_present,
    .request_focus = handle_gtk_surface_request_focus,
};

/**
 * Initializes a gtk_surface object and passes it to the client.
 */
static void handle_gtk_shell_get_gtk_surface(wl_client *client,
    wl_resource *resource, uint32_t id,
    wl_resource *surface)
{
    wf_gtk_surface *gtk_surface = new wf_gtk_surface;
    gtk_surface->resource = wl_resource_create(client, &gtk_surface1_interface,
        wl_resource_get_version(resource), id);
    gtk_surface->wl_surface = surface;
    wl_resource_set_implementation(gtk_surface->resource, &gtk_surface1_impl,
        gtk_surface, handle_gtk_surface_destroy);

    wlr_surface *wlr_surface     = wlr_surface_from_resource(surface);
    wlr_xdg_surface *xdg_surface = wlr_xdg_surface_from_wlr_surface(wlr_surface);
    gtk_surface->on_configure.set_callback([=] (void*)
    {
        handle_xdg_surface_on_configure(gtk_surface);
    });
    gtk_surface->on_configure.connect(&xdg_surface->events.configure);
    gtk_surface->on_destroy.set_callback([=] (void*)
    {
        handle_xdg_surface_on_destroy(gtk_surface);
    });
    gtk_surface->on_destroy.connect(&xdg_surface->events.destroy);
}

/**
 *  Supplements the request_focus() and present()
 *  to prevent focus stealing if user interaction happened
 *  between the time application was called and request_focus was received.
 *  Not implemented.
 */
static void handle_gtk_shell_notify_launch(wl_client *client, wl_resource *resource,
    const char *startup_id)
{}


/**
 *  A view could use this to receive notification when the surface is ready.
 *  Gets the DESKTOP_STARTUP_ID from environment and unsets this env var afterwards
 *  so any child processes don't inherit it.
 *  Not implemented.
 */
static void handle_gtk_shell_set_startup_id(wl_client *client, wl_resource *resource,
    const char *startup_id)
{}


/**
 *  A view could use this to invoke the system bell, be it aural, visual or none at
 * all.
 */
static void handle_gtk_shell_system_bell(wl_client *client, wl_resource *resource,
    wl_resource *surface)
{
    wf::view_system_bell_signal data;
    if (surface)
    {
        auto gtk_surface =
            static_cast<wf_gtk_surface*>(wl_resource_get_user_data(surface));
        data.view = wf::wl_surface_to_wayfire_view(gtk_surface->wl_surface);
    }

    wf::get_core().emit_signal("view-system-bell", &data);
}

/**
 * Supported functions of the gtk_shell_interface implementation
 */
static const struct gtk_shell1_interface gtk_shell1_impl = {
    .get_gtk_surface = handle_gtk_shell_get_gtk_surface,
    .set_startup_id  = handle_gtk_shell_set_startup_id,
    .system_bell     = handle_gtk_shell_system_bell,
    .notify_launch   = handle_gtk_shell_notify_launch,
};


/**
 * Destroy the gtk_shell object.
 * gtk_shell exists as long as the compositor runs.
 */
static void handle_gtk_shell1_destroy(wl_resource *resource)
{}


/**
 * Binds the gtk_shell to wayland.
 */
void bind_gtk_shell1(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    auto resource = wl_resource_create(client, &gtk_shell1_interface,
        GTK_SHELL_VERSION, id);
    wl_resource_set_implementation(resource, &gtk_shell1_impl, data,
        handle_gtk_shell1_destroy);
}

/**
 * Creates a new wf_gtk_shell object.
 * There is one in the compositor initialized at compositor startup.
 */
wf_gtk_shell *wf_gtk_shell_create(wl_display *display)
{
    wf_gtk_shell *gtk_shell = new wf_gtk_shell;
    wl_global *global = wl_global_create(display, &gtk_shell1_interface,
        GTK_SHELL_VERSION, gtk_shell,
        bind_gtk_shell1);
    if (global == NULL)
    {
        LOGE("Failed to create gtk_shell");

        return nullptr;
    }

    return gtk_shell;
}

/**
 * Get's a 'fixed' app_id for some gnome-clients
 * to match the app_id with the desktop file.
 */
std::string wf_gtk_shell_get_custom_app_id(wf_gtk_shell *shell, wl_resource *surface)
{
    return shell->surface_app_id[surface];
}

std::string get_gtk_shell_app_id(wayfire_view view)
{
    if (!view)
    {
        return "";
    }

    auto surface = view->get_wlr_surface();

    if (!surface)
    {
        return "";
    }

    return wf_gtk_shell_get_custom_app_id(
        wf::get_core_impl().gtk_shell, surface->resource);
}
