#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/output-layout.hpp>
#include "wayfire/util.hpp"
#include "wayfire/view.hpp"
#include <map>
#include <memory>
#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>
#include <wayfire/plugin.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>
#include "gtk-shell.hpp"
#include "config.h"

class wayfire_foreign_toplevel;

std::string get_app_id(wayfire_view view)
{
    if (!view)
    {
        return "unknown";
    }

    std::string app_id;
    auto default_app_id = view->get_app_id();

    gtk_shell_app_id_query_signal ev;
    ev.view = view;
    wf::get_core().emit(&ev);
    std::string app_id_mode = wf::option_wrapper_t<std::string>("workarounds/app_id_mode");

    if ((app_id_mode == "gtk-shell") && !ev.app_id.empty())
    {
        app_id = ev.app_id;
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
        app_id = default_app_id + " " + ev.app_id + " wf-ipc-" + std::to_string(view->get_id());
    } else
    {
        app_id = !default_app_id.empty() ? default_app_id : "unknown";
    }

    return app_id;
}

class wayfire_foreign_toplevel
{
    wayfire_toplevel_view view;
    wlr_ext_foreign_toplevel_handle_v1 *handle;

  public:
    wayfire_foreign_toplevel(wayfire_toplevel_view view, wlr_ext_foreign_toplevel_handle_v1 *handle)
    {
        this->view   = view;
        this->handle = handle;

        send_state();

        view->connect(&on_title_changed);
        view->connect(&on_app_id_changed);
    }

    ~wayfire_foreign_toplevel()
    {
        wlr_ext_foreign_toplevel_handle_v1_destroy(handle);
    }

  private:
    void send_state()
    {
        std::string title_buffer = view->get_title();
        std::string appid_buffer = get_app_id(view);

        char title_cstr[1024] = {0};
        char appid_cstr[1024] = {0};

        strcpy(title_cstr, title_buffer.c_str());
        strcpy(appid_cstr, appid_buffer.c_str());

        // Send the state
        struct wlr_ext_foreign_toplevel_handle_v1_state new_state
        {
            title_cstr,
            appid_cstr
        };

        /** done() is sent by wlroots */
        wlr_ext_foreign_toplevel_handle_v1_update_state(handle, &new_state);
    }

    wf::signal::connection_t<wf::view_title_changed_signal> on_title_changed = [=] (auto)
    {
        send_state();
    };

    wf::signal::connection_t<wf::view_app_id_changed_signal> on_app_id_changed = [=] (auto)
    {
        send_state();
    };
};

class wayfire_ext_foreign_toplevel_protocol_impl : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        toplevel_manager = wlr_ext_foreign_toplevel_list_v1_create(wf::get_core().display, 1);
        if (!toplevel_manager)
        {
            LOGE("Failed to create foreign toplevel manager");
            return;
        }

        wf::get_core().connect(&on_view_mapped);
        wf::get_core().connect(&on_view_unmapped);
    }

    void fini() override
    {
        // Clear the toplevel handle pointers.
        handle_for_view.clear();

        // toplevel_manager will be cleared by wlroots.
    }

    bool is_unloadable() override
    {
        return false;
    }

  private:
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            struct wlr_ext_foreign_toplevel_handle_v1_state new_state
            {
                toplevel->get_title().c_str(),
                get_app_id(toplevel).c_str()
            };

            auto handle = wlr_ext_foreign_toplevel_handle_v1_create(toplevel_manager, &new_state);
            if (!handle)
            {
                LOGE("Failed to create foreign toplevel handle for view");
                return;
            }

            handle_for_view[toplevel] = std::make_unique<wayfire_foreign_toplevel>(toplevel, handle);
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        handle_for_view.erase(toplevel_cast(ev->view));
    };

    wlr_ext_foreign_toplevel_list_v1 *toplevel_manager;
    std::map<wayfire_toplevel_view, std::unique_ptr<wayfire_foreign_toplevel>> handle_for_view;
};

DECLARE_WAYFIRE_PLUGIN(wayfire_ext_foreign_toplevel_protocol_impl);
