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
#include "config.h"

#include "toplevel-common.hpp"

class wayfire_ext_foreign_toplevel;
using foreign_toplevel_map_type = std::map<wayfire_toplevel_view, std::unique_ptr<wayfire_ext_foreign_toplevel>>;

void get_state(wayfire_view view, struct wlr_ext_foreign_toplevel_handle_v1_state *state)
{
    std::string title_buffer = view->get_title();
    std::string appid_buffer = get_app_id(view);

    // Update the state
    state->title  = strdup(title_buffer.c_str());
    state->app_id = strdup(appid_buffer.c_str());
}

class wayfire_ext_foreign_toplevel
{
    wayfire_toplevel_view view;
    wlr_ext_foreign_toplevel_handle_v1 *handle;
    wlr_ext_foreign_toplevel_state_handle_v1 *state_handle;
    foreign_toplevel_map_type *view_to_toplevel;

  public:
    wayfire_ext_foreign_toplevel(wayfire_toplevel_view view, wlr_ext_foreign_toplevel_handle_v1 *hndl,
            wlr_ext_foreign_toplevel_state_handle_v1 *st, foreign_toplevel_map_type *view_map) :
        view(view),
        handle(hndl),
        state_handle(st),
        view_to_toplevel(view_map)
    {
        /**
         * This is future-proofing.
         * We can add support for ext-foreign-toplevel-management
         * eventually here, without major changes
         */
        init_request_handlers();

        /**
         * Send the initial state.
         * Currently, only title and app_id need to be sent.
         * Once other ext-foreign-toplevel-* protocols are made
         * available, we will add support for those, here.
         */
        send_initial_state();

        /** Connect various view signals to their handlers */
        init_connections();
    }

    virtual ~wayfire_ext_foreign_toplevel()
    {
        disconnect_request_handlers();
        destroy_handle();
    }

  protected:
    virtual void init_request_handlers()
    {
        toplevel_handle_v1_maximize_request.set_callback([&] (void *data)
        {
            auto ev = static_cast<wlr_ext_foreign_toplevel_state_handle_v1_maximized_event*>(data);
            wf::get_core().default_wm->tile_request(view, ev->maximized ? wf::TILED_EDGES_ALL : 0);
        });

        toplevel_handle_v1_minimize_request.set_callback([&] (void *data)
        {
            auto ev = static_cast<wlr_ext_foreign_toplevel_state_handle_v1_minimized_event*>(data);
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
            auto ev = static_cast<wlr_ext_foreign_toplevel_state_handle_v1_set_rectangle_event*>(data);
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
            auto ev = static_cast<wlr_ext_foreign_toplevel_state_handle_v1_fullscreen_event*>(data);
            auto wo = wf::get_core().output_layout->find_output(ev->output);
            wf::get_core().default_wm->fullscreen_request(view, wo, ev->fullscreen);
        });
        
        toplevel_handle_v1_close_request.connect(&state_handle->events.request_close);
        toplevel_handle_v1_maximize_request.connect(&state_handle->events.request_maximize);
        toplevel_handle_v1_minimize_request.connect(&state_handle->events.request_minimize);
        toplevel_handle_v1_activate_request.connect(&state_handle->events.request_activate);
        toplevel_handle_v1_fullscreen_request.connect(&state_handle->events.request_fullscreen);
        toplevel_handle_v1_set_rectangle_request.connect(&state_handle->events.set_rectangle);
    }

    virtual void send_initial_state()
    {
        toplevel_send_title_appid();
        toplevel_send_additional_state();
    }

    virtual void init_connections()
    {
        view->connect(&on_title_changed);
        view->connect(&on_app_id_changed);
        view->connect(&on_tiled);
        view->connect(&on_minimized);
        view->connect(&on_fullscreen);
        view->connect(&on_activated);
        view->connect(&on_parent_changed);
    }

    virtual void disconnect_request_handlers()
    {
        toplevel_handle_v1_close_request.disconnect();
        toplevel_handle_v1_maximize_request.disconnect();
        toplevel_handle_v1_minimize_request.disconnect();
        toplevel_handle_v1_activate_request.disconnect();
        toplevel_handle_v1_fullscreen_request.disconnect();
        toplevel_handle_v1_set_rectangle_request.disconnect();
    }

    virtual void destroy_handle()
    {
        wlr_ext_foreign_toplevel_state_handle_v1_destroy(state_handle);
        wlr_ext_foreign_toplevel_handle_v1_destroy(handle);
    }

    virtual void toplevel_send_title_appid()
    {
        // Prepare the state
        struct wlr_ext_foreign_toplevel_handle_v1_state new_state;
        get_state(view, &new_state);

        /** Send the state; done() is sent by wlroots */
        wlr_ext_foreign_toplevel_handle_v1_update_state(handle,
            &new_state);
    }
        
    virtual void toplevel_send_additional_state()
    {
        /** Additional state (note: these are no-ops if there is not change from the current state */
        wlr_ext_foreign_toplevel_state_handle_v1_set_maximized(state_handle, view->pending_tiled_edges() == wf::TILED_EDGES_ALL);
        wlr_ext_foreign_toplevel_state_handle_v1_set_activated(state_handle, view->activated);
        wlr_ext_foreign_toplevel_state_handle_v1_set_minimized(state_handle, view->minimized);
        wlr_ext_foreign_toplevel_state_handle_v1_set_fullscreen(state_handle, view->pending_fullscreen());
        //!! TODO: always on top and sticky !!

        /* update parent as well */
        auto it = view_to_toplevel->find(view->parent);
        if (it == view_to_toplevel->end())
        {
            wlr_ext_foreign_toplevel_state_handle_v1_set_parent(state_handle, nullptr);
        } else
        {
            wlr_ext_foreign_toplevel_state_handle_v1_set_parent(state_handle, it->second->state_handle);
        }
    }

    wf::signal::connection_t<wf::view_title_changed_signal> on_title_changed = [=] (auto)
    {
        toplevel_send_title_appid();
    };

    wf::signal::connection_t<wf::view_app_id_changed_signal> on_app_id_changed = [=] (auto)
    {
        toplevel_send_title_appid();
    };

    wf::signal::connection_t<wf::view_minimized_signal> on_minimized = [=] (auto)
    {
        toplevel_send_additional_state();
    };

    wf::signal::connection_t<wf::view_fullscreen_signal> on_fullscreen = [=] (auto)
    {
        toplevel_send_additional_state();
    };

    wf::signal::connection_t<wf::view_tiled_signal> on_tiled = [=] (auto)
    {
        toplevel_send_additional_state();
    };

    wf::signal::connection_t<wf::view_activated_state_signal> on_activated = [=] (auto)
    {
        toplevel_send_additional_state();
    };

    wf::signal::connection_t<wf::view_parent_changed_signal> on_parent_changed = [=] (auto)
    {
        toplevel_send_additional_state();
    };

    wf::wl_listener_wrapper toplevel_handle_v1_maximize_request;
    wf::wl_listener_wrapper toplevel_handle_v1_activate_request;
    wf::wl_listener_wrapper toplevel_handle_v1_minimize_request;
    wf::wl_listener_wrapper toplevel_handle_v1_set_rectangle_request;
    wf::wl_listener_wrapper toplevel_handle_v1_fullscreen_request;
    wf::wl_listener_wrapper toplevel_handle_v1_close_request;

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

        state_manager = wlr_ext_foreign_toplevel_state_manager_v1_create(wf::get_core().display,
            15, // possible notifications: maximize, minimize, activated, fullscreen -- TODO: use #defines from protocol header !!
            31 // possible actions: close, maximize, minimize, activate, fullscreen
        );
        if (!state_manager)
        {
            LOGE("Failed to create foreign toplevel state manager");
            //!! TODO: is there no way to destroy toplevel_manager ??
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
            struct wlr_ext_foreign_toplevel_handle_v1_state new_state;
            get_state(toplevel, &new_state);

            auto handle = wlr_ext_foreign_toplevel_handle_v1_create(toplevel_manager, &new_state);
            if (!handle)
            {
                LOGE("Failed to create foreign toplevel handle for view");
                return;
            }

            auto state = wlr_ext_foreign_toplevel_state_handle_v1_create (state_manager, handle);
            if (!state)
            {
                LOGE("Failed to create foreign toplevel state handle for view");
                wlr_ext_foreign_toplevel_handle_v1_destroy (handle);
                return;
            }

            handle_for_view[toplevel] = std::make_unique<wayfire_ext_foreign_toplevel>(toplevel, handle, state, &handle_for_view);
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        handle_for_view.erase(toplevel_cast(ev->view));
    };

    wlr_ext_foreign_toplevel_list_v1 *toplevel_manager;
    wlr_ext_foreign_toplevel_state_manager_v1 *state_manager;
    std::map<wayfire_toplevel_view, std::unique_ptr<wayfire_ext_foreign_toplevel>> handle_for_view;
};

DECLARE_WAYFIRE_PLUGIN(wayfire_ext_foreign_toplevel_protocol_impl);
