#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/toplevel-view.hpp>
#include <set>

#include "plugins/ipc/ipc-helpers.hpp"
#include "plugins/ipc/ipc.hpp"
#include "plugins/ipc/ipc-method-repository.hpp"
#include "wayfire/core.hpp"
#include "wayfire/object.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/workarea.hpp"
#include "wayfire/workspace-set.hpp"
#include "config.h"
#include <wayfire/nonstd/wlroots-full.hpp>

class ipc_rules_t : public wf::plugin_interface_t, public wf::per_output_tracker_mixin_t<>
{
  public:
    void init() override
    {
        method_repository->register_method("window-rules/events/watch", on_client_watch);
        method_repository->register_method("window-rules/view-info", get_view_info);
        method_repository->register_method("window-rules/output-info", get_output_info);
        method_repository->register_method("window-rules/configure-view", configure_view);
        ipc_server->connect(&on_client_disconnected);
        wf::get_core().connect(&on_view_mapped);
        wf::get_core().connect(&on_kbfocus_changed);
        init_output_tracking();
    }

    void fini() override
    {
        method_repository->unregister_method("window-rules/events/watch");
        method_repository->unregister_method("window-rules/view-info");
        method_repository->unregister_method("window-rules/output-info");
        method_repository->unregister_method("window-rules/configure-view");
        fini_output_tracking();
    }

    void handle_new_output(wf::output_t *output) override
    {
        output->connect(&_tiled);
        output->connect(&_minimized);
        output->connect(&_fullscreened);
    }

    void handle_output_removed(wf::output_t *output) override
    {
        // no-op
    }

    wf::ipc::method_callback get_view_info = [=] (nlohmann::json data)
    {
        WFJSON_EXPECT_FIELD(data, "id", number_integer);
        if (auto view = wf::ipc::find_view_by_id(data["id"]))
        {
            auto response = wf::ipc::json_ok();
            response["info"] = view_to_json(view);
            return response;
        }

        return wf::ipc::json_error("no such view");
    };

    wf::ipc::method_callback get_output_info = [=] (nlohmann::json data)
    {
        WFJSON_EXPECT_FIELD(data, "id", number_integer);
        auto wo = wf::ipc::find_output_by_id(data["id"]);
        if (!wo)
        {
            return wf::ipc::json_error("output not found");
        }

        auto response = wf::ipc::json_ok();
        response["name"]     = wo->to_string();
        response["geometry"] = wf::ipc::geometry_to_json(wo->get_layout_geometry());
        response["workarea"] = wf::ipc::geometry_to_json(wo->workarea->get_workarea());
        response["workspace"]["x"] = wo->wset()->get_current_workspace().x;
        response["workspace"]["y"] = wo->wset()->get_current_workspace().y;
        response["workspace"]["grid_width"]  = wo->wset()->get_workspace_grid_size().width;
        response["workspace"]["grid_height"] = wo->wset()->get_workspace_grid_size().height;
        return response;
    };

    wf::ipc::method_callback configure_view = [=] (nlohmann::json data)
    {
        WFJSON_EXPECT_FIELD(data, "id", number_integer);
        WFJSON_OPTIONAL_FIELD(data, "output_id", number_integer);
        WFJSON_OPTIONAL_FIELD(data, "geometry", object);

        auto view = wf::ipc::find_view_by_id(data["id"]);
        if (!view)
        {
            return wf::ipc::json_error("view not found");
        }

        auto toplevel = wf::toplevel_cast(view);
        if (!toplevel)
        {
            return wf::ipc::json_error("view is not toplevel");
        }

        if (data.contains("output_id"))
        {
            auto wo = wf::ipc::find_output_by_id(data["output_id"]);
            if (!wo)
            {
                return wf::ipc::json_error("output not found");
            }

            wf::move_view_to_output(toplevel, wo, !data.contains("geometry"));
        }

        if (data.contains("geometry"))
        {
            auto geometry = wf::ipc::geometry_from_json(data["geometry"]);
            if (!geometry)
            {
                return wf::ipc::json_error("invalid geometry");
            }

            toplevel->set_geometry(*geometry);
        }

        return wf::ipc::json_ok();
    };

  private:
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> method_repository;
    wf::shared_data::ref_ptr_t<wf::ipc::server_t> ipc_server;

    // Track a list of clients which have requested watch
    std::set<wf::ipc::client_t*> clients;

    wf::ipc::method_callback on_client_watch = [=] (nlohmann::json data)
    {
        clients.insert(ipc_server->get_current_request_client());
        return wf::ipc::json_ok();
    };

    wf::signal::connection_t<wf::ipc::client_disconnected_signal> on_client_disconnected =
        [=] (wf::ipc::client_disconnected_signal *ev)
    {
        clients.erase(ev->client);
    };

    void send_view_to_subscribes(wayfire_view view, std::string event_name)
    {
        nlohmann::json event;
        event["event"] = event_name;
        event["view"]  = view_to_json(view);
        for (auto& client : clients)
        {
            client->send_json(event);
        }
    }

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-mapped");
    };

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_kbfocus_changed =
        [=] (wf::keyboard_focus_changed_signal *ev)
    {
        send_view_to_subscribes(wf::node_to_view(ev->new_focus), "view-focused");
    };

    // Maximized rule handler.
    wf::signal::connection_t<wf::view_tiled_signal> _tiled = [=] (wf::view_tiled_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-tiled");
    };

    // Minimized rule handler.
    wf::signal::connection_t<wf::view_minimized_signal> _minimized = [=] (wf::view_minimized_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-minimized");
    };

    // Fullscreened rule handler.
    wf::signal::connection_t<wf::view_fullscreen_signal> _fullscreened = [=] (wf::view_fullscreen_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-fullscreen");
    };

    std::string get_view_type(wayfire_view view)
    {
        if (view->role == wf::VIEW_ROLE_TOPLEVEL)
        {
            return "toplevel";
        }

        if (view->role == wf::VIEW_ROLE_UNMANAGED)
        {
#if WF_HAS_XWAYLAND
            auto surf = view->get_wlr_surface();
            if (surf && wlr_surface_is_xwayland_surface(surf))
            {
                return "x-or";
            }

#endif

            return "unmanaged";
        }

        auto layer = wf::get_view_layer(view);
        if ((layer == wf::scene::layer::BACKGROUND) || (layer == wf::scene::layer::BOTTOM))
        {
            return "background";
        } else if (layer == wf::scene::layer::TOP)
        {
            return "panel";
        } else if (layer == wf::scene::layer::OVERLAY)
        {
            return "overlay";
        }

        return "unknown";
    }

    nlohmann::json view_to_json(wayfire_view view)
    {
        if (!view)
        {
            return nullptr;
        }

        nlohmann::json description;
        description["id"]     = view->get_id();
        description["app-id"] = view->get_app_id();
        description["title"]  = view->get_title();
        auto toplevel = wf::toplevel_cast(view);
        description["geometry"] =
            wf::ipc::geometry_to_json(toplevel ? toplevel->get_pending_geometry() : view->get_bounding_box());
        description["output"] = view->get_output() ? view->get_output()->get_id() : -1;
        description["tiled-edges"] = toplevel ? toplevel->pending_tiled_edges() : 0;
        description["fullscreen"]  = toplevel ? toplevel->pending_fullscreen() : false;
        description["minimized"]   = toplevel ? toplevel->minimized : false;
        description["focusable"]   = view->is_focusable();
        description["type"] = get_view_type(view);
        return description;
    }
};

DECLARE_WAYFIRE_PLUGIN(ipc_rules_t);
