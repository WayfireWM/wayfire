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
