#pragma once

#include <string>
#include <wayfire/view.hpp>

/**
 * Get's a 'fixed' app_id for some gnome-clients
 * to match the app_id with the desktop file.
 * Not all clients provide this in which case
 * an empty string will be returned - ""
 */
std::string get_gtk_shell_app_id(wayfire_view view);

std::string get_gtk_shell_app_menu_path(wayfire_view view);

std::string get_gtk_shell_menubar_path(wayfire_view view);

std::string get_gtk_shell_dbus_name(wayfire_view view);