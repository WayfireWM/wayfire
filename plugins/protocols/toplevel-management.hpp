#include "wayfire/view.hpp"
#include <wayfire/toplevel-view.hpp>

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

class wayfire_foreign_toplevel
{
  public:
    wayfire_foreign_toplevel(wayfire_toplevel_view view) :
        view(view)
    {
        /** Currently only wlr-foreign-toplevel-handle needs this. */
        init_request_handlers();

        /** wlr-* and ext-* have different data to be sent */
        send_initial_state();

        /** Connect various view signals to their handlers */
        init_connections();
    }

    ~wayfire_foreign_toplevel()
    {
        disconnect_request_handlers();
        destroy_handle();
    }

  protected:
    wayfire_toplevel_view view;
    /** This can be wither wlr_foreign_toplevel_handle_v1 or ext_foreign_toplevel_handle_v1 */

    virtual void init_request_handlers()
    {}
    virtual void send_initial_state()
    {}
    virtual void init_connections()
    {}
    virtual void disconnect_request_handlers()
    {}
    virtual void destroy_handle()
    {}

    virtual void toplevel_send_title()
    {}
    virtual void toplevel_send_app_id()
    {}
    virtual void toplevel_update_output(wf::output_t*, bool)
    {}
    virtual void toplevel_send_state()
    {}

    /** Minimize rectangle */
    virtual void handle_minimize_hint(wf::toplevel_view_interface_t *view, wf::view_interface_t *relative_to,
        wlr_box hint)
    {}

    wf::signal::connection_t<wf::view_title_changed_signal> on_title_changed = [=] (auto)
    {
        toplevel_send_title();
    };

    wf::signal::connection_t<wf::view_app_id_changed_signal> on_app_id_changed = [=] (auto)
    {
        toplevel_send_app_id();
    };

    wf::signal::connection_t<wf::view_set_output_signal> on_set_output = [=] (wf::view_set_output_signal *ev)
    {
        toplevel_update_output(ev->output, false);
        toplevel_update_output(view->get_output(), true);
    };

    wf::signal::connection_t<wf::view_minimized_signal> on_minimized = [=] (auto)
    {
        toplevel_send_state();
    };

    wf::signal::connection_t<wf::view_fullscreen_signal> on_fullscreen = [=] (auto)
    {
        toplevel_send_state();
    };

    wf::signal::connection_t<wf::view_tiled_signal> on_tiled = [=] (auto)
    {
        toplevel_send_state();
    };

    wf::signal::connection_t<wf::view_activated_state_signal> on_activated = [=] (auto)
    {
        toplevel_send_state();
    };

    wf::signal::connection_t<wf::view_parent_changed_signal> on_parent_changed = [=] (auto)
    {
        toplevel_send_state();
    };

    wf::wl_listener_wrapper toplevel_handle_v1_maximize_request;
    wf::wl_listener_wrapper toplevel_handle_v1_activate_request;
    wf::wl_listener_wrapper toplevel_handle_v1_minimize_request;
    wf::wl_listener_wrapper toplevel_handle_v1_set_rectangle_request;
    wf::wl_listener_wrapper toplevel_handle_v1_fullscreen_request;
    wf::wl_listener_wrapper toplevel_handle_v1_close_request;
};
