#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/output-layout.hpp>
#include "wayfire/util.hpp"
#include "wayfire/view.hpp"
#include <memory>
#include <wayfire/plugin.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>
#include "gtk-shell.hpp"
#include "kde-appmenu.hpp"
#include "config.h"

class wayfire_foreign_toplevel;
using foreign_toplevel_map_type = std::map<wayfire_toplevel_view, std::unique_ptr<wayfire_foreign_toplevel>>;

class toplevel_gtk_shell1_dbus_properties_t : public wf::custom_data_t
{
  public:
    std::optional<std::string> app_menu_path;
    std::optional<std::string> menubar_path;
    std::optional<std::string> window_object_path;
    std::optional<std::string> application_object_path;
    std::optional<std::string> unique_bus_name;
};

class toplevel_kde_appmenu_path_t : public wf::custom_data_t
{
  public:
    std::optional<std::string> service_name;
    std::optional<std::string> object_path;
};

class wayfire_foreign_toplevel
{
    wayfire_toplevel_view view;
    wlr_foreign_toplevel_handle_v1 *handle;
    foreign_toplevel_map_type *view_to_toplevel;

  public:
    wayfire_foreign_toplevel(wayfire_toplevel_view view, wlr_foreign_toplevel_handle_v1 *handle,
        foreign_toplevel_map_type *view_to_toplevel)
    {
        this->view   = view;
        this->handle = handle;
        this->view_to_toplevel = view_to_toplevel;

        init_request_handlers();
        toplevel_handle_v1_close_request.connect(&handle->events.request_close);
        toplevel_handle_v1_maximize_request.connect(&handle->events.request_maximize);
        toplevel_handle_v1_minimize_request.connect(&handle->events.request_minimize);
        toplevel_handle_v1_activate_request.connect(&handle->events.request_activate);
        toplevel_handle_v1_fullscreen_request.connect(&handle->events.request_fullscreen);
        toplevel_handle_v1_set_rectangle_request.connect(&handle->events.set_rectangle);

        toplevel_send_title();
        toplevel_send_app_id();
        toplevel_send_state();
        toplevel_update_output(view->get_output(), true);

        view->connect(&on_title_changed);
        view->connect(&on_app_id_changed);

        view->connect(&on_set_output);
        view->connect(&on_tiled);
        view->connect(&on_minimized);
        view->connect(&on_fullscreen);
        view->connect(&on_activated);
        view->connect(&on_parent_changed);
    }

    ~wayfire_foreign_toplevel()
    {
        toplevel_handle_v1_close_request.disconnect();
        toplevel_handle_v1_maximize_request.disconnect();
        toplevel_handle_v1_minimize_request.disconnect();
        toplevel_handle_v1_activate_request.disconnect();
        toplevel_handle_v1_fullscreen_request.disconnect();
        toplevel_handle_v1_set_rectangle_request.disconnect();
        wlr_foreign_toplevel_handle_v1_destroy(handle);
    }

    void toplevel_send_gtk_shell1_dbus_properties(
        const char *app_menu_path,
        const char *menubar_path,
        const char *window_object_path,
        const char *application_object_path,
        const char *unique_bus_name)
    {
        // !! TODO: app_menu_path (not sure which interface it corresponds to)

        if (menubar_path && unique_bus_name)
        {
            wlr_foreign_toplevel_handle_v1_add_surface_dbus_annotation(
                handle, "org.gtk.Menus", unique_bus_name, menubar_path);
        } else
        {
            wlr_foreign_toplevel_handle_v1_remove_surface_dbus_annotation(
                handle, "org.gtk.Menus");
        }

        if (window_object_path && unique_bus_name)
        {
            wlr_foreign_toplevel_handle_v1_add_surface_dbus_annotation(
                handle, "org.gtk.Actions", unique_bus_name, window_object_path);
        } else
        {
            wlr_foreign_toplevel_handle_v1_remove_surface_dbus_annotation(
                handle, "org.gtk.Actions");
        }

        if (application_object_path && unique_bus_name)
        {
            wlr_foreign_toplevel_handle_v1_add_client_dbus_annotation(
                handle, "org.gtk.Actions", unique_bus_name, application_object_path);
        } else
        {
            wlr_foreign_toplevel_handle_v1_remove_client_dbus_annotation(
                handle, "org.gtk.Actions");
        }
    }

    void toplevel_send_kde_appmenu_path(const char *service_name, const char *object_path)
    {
        if (service_name && object_path)
        {
            wlr_foreign_toplevel_handle_v1_add_surface_dbus_annotation(
                handle, "com.canonical.dbusmenu", service_name, object_path);
        } else
        {
            wlr_foreign_toplevel_handle_v1_remove_surface_dbus_annotation(
                handle, "com.canonical.dbusmenu");
        }
    }

  private:
    void toplevel_send_title()
    {
        wlr_foreign_toplevel_handle_v1_set_title(handle, view->get_title().c_str());
    }

    void toplevel_send_app_id()
    {
        std::string app_id;

        auto default_app_id = view->get_app_id();

        gtk_shell_app_id_query_signal ev;
        ev.view = view;
        wf::get_core().emit(&ev);
        std::string app_id_mode = wf::option_wrapper_t<std::string>("workarounds/app_id_mode");

        if ((app_id_mode == "gtk-shell") && (ev.app_id.length() > 0))
        {
            app_id = ev.app_id;
        } else if (app_id_mode == "full")
        {
#if WF_HAS_XWAYLAND
            auto wlr_surface = view->get_wlr_surface();
            if (wlr_surface)
            {
                if (wlr_xwayland_surface *xw_surface =
                        wlr_xwayland_surface_try_from_wlr_surface(wlr_surface))
                {
                    ev.app_id = nonull(xw_surface->instance);
                }
            }

#endif

            app_id = default_app_id + " " + ev.app_id + " wf-ipc-" + std::to_string(view->get_id());
        } else
        {
            app_id = default_app_id;
        }

        wlr_foreign_toplevel_handle_v1_set_app_id(handle, app_id.c_str());
    }

    void toplevel_send_state()
    {
        wlr_foreign_toplevel_handle_v1_set_maximized(handle,
            view->pending_tiled_edges() == wf::TILED_EDGES_ALL);
        wlr_foreign_toplevel_handle_v1_set_activated(handle, view->activated);
        wlr_foreign_toplevel_handle_v1_set_minimized(handle, view->minimized);
        wlr_foreign_toplevel_handle_v1_set_fullscreen(handle, view->pending_fullscreen());

        /* update parent as well */
        auto it = view_to_toplevel->find(view->parent);
        if (it == view_to_toplevel->end())
        {
            wlr_foreign_toplevel_handle_v1_set_parent(handle, nullptr);
        } else
        {
            wlr_foreign_toplevel_handle_v1_set_parent(handle, it->second->handle);
        }
    }

    void toplevel_update_output(wf::output_t *output, bool enter)
    {
        if (output && enter)
        {
            wlr_foreign_toplevel_handle_v1_output_enter(handle, output->handle);
        }

        if (output && !enter)
        {
            wlr_foreign_toplevel_handle_v1_output_leave(handle, output->handle);
        }
    }

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

    void init_request_handlers()
    {
        toplevel_handle_v1_maximize_request.set_callback([&] (void *data)
        {
            auto ev = static_cast<wlr_foreign_toplevel_handle_v1_maximized_event*>(data);
            wf::get_core().default_wm->tile_request(view, ev->maximized ? wf::TILED_EDGES_ALL : 0);
        });

        toplevel_handle_v1_minimize_request.set_callback([&] (void *data)
        {
            auto ev = static_cast<wlr_foreign_toplevel_handle_v1_minimized_event*>(data);
            wf::get_core().default_wm->minimize_request(view, ev->minimized);
        });

        toplevel_handle_v1_activate_request.set_callback([&] (auto)
        {
            wf::get_core().default_wm->focus_request(view);
        });

        toplevel_handle_v1_close_request.set_callback([&] (auto)
        {
            view->close();
        });

        toplevel_handle_v1_set_rectangle_request.set_callback([&] (void *data)
        {
            auto ev = static_cast<wlr_foreign_toplevel_handle_v1_set_rectangle_event*>(data);
            auto surface = wf::wl_surface_to_wayfire_view(ev->surface->resource);
            if (!surface)
            {
                LOGE("Setting minimize hint to unknown surface. Wayfire currently"
                     "supports only setting hints relative to views.");
                return;
            }

            handle_minimize_hint(view.get(), surface.get(), {ev->x, ev->y, ev->width, ev->height});
        });

        toplevel_handle_v1_fullscreen_request.set_callback([&] (
            void *data)
        {
            auto ev = static_cast<wlr_foreign_toplevel_handle_v1_fullscreen_event*>(data);
            auto wo = wf::get_core().output_layout->find_output(ev->output);
            wf::get_core().default_wm->fullscreen_request(view, wo, ev->fullscreen);
        });
    }

    void handle_minimize_hint(wf::toplevel_view_interface_t *view, wf::view_interface_t *relative_to,
        wlr_box hint)
    {
        if (relative_to->get_output() != view->get_output())
        {
            LOGE("Minimize hint set to surface on a different output, " "problems might arise");
            /* TODO: translate coordinates in case minimize hint is on another output */
        }

        wf::pointf_t relative = relative_to->get_surface_root_node()->to_global({0, 0});
        hint.x += relative.x;
        hint.y += relative.y;
        view->set_minimize_hint(hint);
    }
};

class wayfire_foreign_toplevel_protocol_impl : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        toplevel_manager = wlr_foreign_toplevel_manager_v1_create(wf::get_core().display);
        wf::get_core().connect(&on_view_mapped);
        wf::get_core().connect(&on_view_unmapped);
        wf::get_core().connect(&on_view_dbus_properties_changed);
        wf::get_core().connect(&on_view_kde_appmenu_changed);
    }

    void fini() override
    {}

    bool is_unloadable() override
    {
        return false;
    }

  private:
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            auto handle = wlr_foreign_toplevel_handle_v1_create(toplevel_manager);
            handle_for_view[toplevel] =
                std::make_unique<wayfire_foreign_toplevel>(toplevel, handle, &handle_for_view);

            if (auto props = toplevel->get_data<toplevel_gtk_shell1_dbus_properties_t>())
            {
                handle_for_view[toplevel]->toplevel_send_gtk_shell1_dbus_properties(
                    props->app_menu_path ? props->app_menu_path->c_str() : nullptr,
                    props->menubar_path ? props->menubar_path->c_str() : nullptr,
                    props->window_object_path ? props->window_object_path->c_str() : nullptr,
                    props->application_object_path ? props->application_object_path->c_str() : nullptr,
                    props->unique_bus_name ? props->unique_bus_name->c_str() : nullptr);
            }

            if (auto props = toplevel->get_data<toplevel_kde_appmenu_path_t>())
            {
                handle_for_view[toplevel]->toplevel_send_kde_appmenu_path(
                    props->service_name ? props->service_name->c_str() : nullptr,
                    props->object_path ? props->object_path->c_str() : nullptr);
            }
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        handle_for_view.erase(toplevel_cast(ev->view));
    };

    wf::signal::connection_t<gtk_shell_dbus_properties_signal> on_view_dbus_properties_changed =
        [=] (gtk_shell_dbus_properties_signal *ev)
    {
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            auto it = handle_for_view.find(toplevel);
            if (it != handle_for_view.end())
            {
                it->second->toplevel_send_gtk_shell1_dbus_properties(
                    ev->app_menu_path,
                    ev->menubar_path,
                    ev->window_object_path,
                    ev->application_object_path,
                    ev->unique_bus_name);
            }

            /* Store the values with the view. This is necessary to cover the cases when either:
             *  (1) the view has not been mapped yet; or (2) the view is later unmapped and remapped
             */
            auto props = toplevel->get_data_safe<toplevel_gtk_shell1_dbus_properties_t>();
            if (ev->app_menu_path)
            {
                props->app_menu_path = ev->app_menu_path;
            } else
            {
                props->app_menu_path.reset();
            }

            if (ev->application_object_path)
            {
                props->application_object_path =
                    ev->application_object_path;
            } else
            {
                props->application_object_path.reset();
            }

            if (ev->menubar_path)
            {
                props->menubar_path = ev->menubar_path;
            } else
            {
                props->menubar_path.reset();
            }

            if (ev->unique_bus_name)
            {
                props->unique_bus_name = ev->unique_bus_name;
            } else
            {
                props->unique_bus_name.reset();
            }

            if (ev->window_object_path)
            {
                props->window_object_path =
                    ev->window_object_path;
            } else
            {
                props->window_object_path.reset();
            }
        }
    };

    wf::signal::connection_t<kde_appmenu_dbus_address_signal> on_view_kde_appmenu_changed =
        [=] (kde_appmenu_dbus_address_signal *ev)
    {
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            auto it = handle_for_view.find(toplevel);
            if (it != handle_for_view.end())
            {
                it->second->toplevel_send_kde_appmenu_path(
                    ev->service_name,
                    ev->object_path);
            }

            /* Store the values with the view. This is necessary to cover the cases when either:
             *  (1) the view has not been mapped yet; or (2) the view is later unmapped and remapped
             */
            auto props = toplevel->get_data_safe<toplevel_kde_appmenu_path_t>();
            if (ev->service_name)
            {
                props->service_name = ev->service_name;
            } else
            {
                props->service_name.reset();
            }

            if (ev->object_path)
            {
                props->object_path = ev->object_path;
            } else
            {
                props->object_path.reset();
            }
        }
    };

    wlr_foreign_toplevel_manager_v1 *toplevel_manager;
    std::map<wayfire_toplevel_view, std::unique_ptr<wayfire_foreign_toplevel>> handle_for_view;
};

DECLARE_WAYFIRE_PLUGIN(wayfire_foreign_toplevel_protocol_impl);
