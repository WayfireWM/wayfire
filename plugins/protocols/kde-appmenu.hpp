#pragma once

#include <wayfire/view.hpp>

/**
 * A signal that is emitted when the DBus address specified for a view
 * via the kde-appmenu protocol changes.
 */
struct kde_appmenu_dbus_address_signal
{
    wayfire_view view;

    const char *service_name;
    const char *object_path;
};
