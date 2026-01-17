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
 * Custom data to store on views the DBus properties set by clients
 * using the gtk-shell protocol.
 */
class gtk_shell_dbus_properties_t : public wf::custom_data_t
{
  public:
    std::optional<std::string> app_menu_path;
    std::optional<std::string> menubar_path;
    std::optional<std::string> window_object_path;
    std::optional<std::string> application_object_path;
    std::optional<std::string> unique_bus_name;
};
