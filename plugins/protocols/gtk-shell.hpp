#pragma once

#include <wayfire/view.hpp>

/**
 * A signal to query the gtk_shell plugin about the gtk-shell-specific app_id of the given view.
 */
struct gtk_shell_app_id_query_signal
{
    wayfire_view view;

    // Set by the gtk-shell plugin in response to the signal
    std::string app_id;
};

/**
 * A signal that is emitted when the DBus properties of a gtk-shell surface change.
 */
struct gtk_shell_dbus_properties_signal
{
    wayfire_view view;

    const char *app_menu_path;
    const char *menubar_path;
    const char *window_object_path;
    const char *application_object_path;
    const char *unique_bus_name;
};
