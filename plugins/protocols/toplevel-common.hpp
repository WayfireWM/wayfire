#include "wayfire/core.hpp"
#include "wayfire/util.hpp"
#include "wayfire/view.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/toplevel-view.hpp>
#include "gtk-shell.hpp"

std::string get_app_id(wayfire_view view)
{
    if (!view)
    {
        return "unknown";
    }

    std::string result;
    auto default_app_id = view->get_app_id();

    gtk_shell_app_id_query_signal ev;
    ev.view = view;
    wf::get_core().emit(&ev);
    std::string app_id_mode = wf::option_wrapper_t<std::string>("workarounds/app_id_mode");

    if ((app_id_mode == "gtk-shell") && !ev.app_id.empty())
    {
        result = ev.app_id;
    } else if (app_id_mode == "full")
    {
#if WF_HAS_XWAYLAND
        auto wlr_surface = view->get_wlr_surface();
        if (wlr_surface)
        {
            if (wlr_xwayland_surface *xw_surface = wlr_xwayland_surface_try_from_wlr_surface(wlr_surface))
            {
                ev.app_id = nonull(xw_surface->instance);
            }
        }

#endif
        result = default_app_id + " " + ev.app_id + " wf-ipc-" + std::to_string(view->get_id());
    } else
    {
        result = !default_app_id.empty() ? default_app_id : "unknown";
    }

    // Safely copy to the output buffer
    return result;
}
