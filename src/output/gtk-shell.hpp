#pragma once

#include <wayland-client.h>
#include <string>
#include <wayfire/view.hpp>

struct wf_gtk_shell;
struct wl_resource;

wf_gtk_shell *wf_gtk_shell_create(wl_display *display);

std::string wf_gtk_shell_get_custom_app_id(wf_gtk_shell *shell,
    wl_resource *surface);

std::string get_gtk_shell_app_menu_path(wayfire_view view);

std::string get_gtk_shell_menubar_path(wayfire_view view);

std::string get_gtk_shell_win_path(wayfire_view view);

std::string get_gtk_shell_app_path(wayfire_view view);

std::string get_gtk_shell_dbus_name(wayfire_view view);
