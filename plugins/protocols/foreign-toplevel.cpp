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

#include "toplevel-management.hpp"

#define WLR_HNDL static_cast<wlr_foreign_toplevel_handle_v1*>(handle)

class wayfire_wlr_foreign_toplevel : public wayfire_foreign_toplevel
{
  public:
    wayfire_wlr_foreign_toplevel(wayfire_toplevel_view view, void *handle,
        foreign_toplevel_map_type *view_to_toplevel) : wayfire_foreign_toplevel(view,
            handle, view_to_toplevel,
            ProtocolType::WLR)
    {}

  protected:
    virtual void send_initial_state() override
    {
        toplevel_send_title();
        toplevel_send_app_id();
        toplevel_send_state();
        toplevel_update_output(view->get_output(), true);
    }

    virtual void init_connections() override
    {
        view->connect(&on_title_changed);
        view->connect(&on_app_id_changed);

        view->connect(&on_set_output);
        view->connect(&on_tiled);
        view->connect(&on_minimized);
        view->connect(&on_fullscreen);
        view->connect(&on_activated);
        view->connect(&on_parent_changed);
    }

    virtual void disconnect_request_handlers() override
    {
        toplevel_handle_v1_close_request.disconnect();
        toplevel_handle_v1_maximize_request.disconnect();
        toplevel_handle_v1_minimize_request.disconnect();
        toplevel_handle_v1_activate_request.disconnect();
        toplevel_handle_v1_fullscreen_request.disconnect();
        toplevel_handle_v1_set_rectangle_request.disconnect();
    }

    virtual void destroy_handle() override
    {
        wlr_foreign_toplevel_handle_v1_destroy(WLR_HNDL);
    }

    virtual void toplevel_send_title() override
    {
        wlr_foreign_toplevel_handle_v1_set_title(WLR_HNDL,
            view->get_title().c_str());
    }

    virtual void toplevel_send_app_id() override
    {
        char app_id[1024] = {0};
        get_app_id(view, app_id);
        wlr_foreign_toplevel_handle_v1_set_app_id(WLR_HNDL, app_id);
    }

    virtual void toplevel_send_state() override
    {
        wlr_foreign_toplevel_handle_v1_set_maximized(WLR_HNDL,
            view->pending_tiled_edges() == wf::TILED_EDGES_ALL);
        wlr_foreign_toplevel_handle_v1_set_activated(WLR_HNDL,
            view->activated);
        wlr_foreign_toplevel_handle_v1_set_minimized(WLR_HNDL,
            view->minimized);
        wlr_foreign_toplevel_handle_v1_set_fullscreen(WLR_HNDL,
            view->pending_fullscreen());

        /* update parent as well */
        auto it = view_to_toplevel->find(view->parent);
        if (it == view_to_toplevel->end())
        {
            wlr_foreign_toplevel_handle_v1_set_parent(WLR_HNDL,
                nullptr);
        } else
        {
            wlr_foreign_toplevel_handle_v1_set_parent(WLR_HNDL,
                static_cast<wlr_foreign_toplevel_handle_v1*>(it->second->get()));
        }
    }

    virtual void toplevel_update_output(wf::output_t *output, bool enter) override
    {
        if (output && enter)
        {
            wlr_foreign_toplevel_handle_v1_output_enter(WLR_HNDL,
                output->handle);
        }

        if (output && !enter)
        {
            wlr_foreign_toplevel_handle_v1_output_leave(WLR_HNDL,
                output->handle);
        }
    }

    virtual void init_request_handlers() override
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

        toplevel_handle_v1_close_request.connect(
            &WLR_HNDL->events.request_close);
        toplevel_handle_v1_maximize_request.connect(
            &WLR_HNDL->events.request_maximize);
        toplevel_handle_v1_minimize_request.connect(
            &WLR_HNDL->events.request_minimize);
        toplevel_handle_v1_activate_request.connect(
            &WLR_HNDL->events.request_activate);
        toplevel_handle_v1_fullscreen_request.connect(
            &WLR_HNDL->events.request_fullscreen);
        toplevel_handle_v1_set_rectangle_request.connect(
            &WLR_HNDL->events.set_rectangle);
    }

    virtual void handle_minimize_hint(wf::toplevel_view_interface_t *view,
        wf::view_interface_t *relative_to,
        wlr_box hint) override
    {
        if (relative_to->get_output() != view->get_output())
        {
            LOGE("Minimize hint set to surface on a different output, problems might arise");
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
            auto handle = wlr_foreign_toplevel_handle_v1_create(toplevel_manager);
            handle_for_view[toplevel] =
                std::make_unique<wayfire_wlr_foreign_toplevel>(toplevel, handle, &handle_for_view);
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        handle_for_view.erase(toplevel_cast(ev->view));
    };

    wlr_foreign_toplevel_manager_v1 *toplevel_manager;
    std::map<wayfire_toplevel_view, std::unique_ptr<wayfire_foreign_toplevel>> handle_for_view;
};

DECLARE_WAYFIRE_PLUGIN(wayfire_foreign_toplevel_protocol_impl);
