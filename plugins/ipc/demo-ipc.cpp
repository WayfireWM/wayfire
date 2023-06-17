#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/toplevel-view.hpp>
#include <set>

#include "ipc-helpers.hpp"
#include "ipc.hpp"
#include "ipc-method-repository.hpp"
#include "wayfire/core.hpp"
#include "wayfire/object.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"

class wayfire_demo_ipc : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        method_repository->register_method("demo-ipc/watch", on_client_watch);
        method_repository->register_method("demo-ipc/view-info", get_view_info);
        method_repository->register_method("demo-ipc/output-info", get_output_info);
        method_repository->register_method("demo-ipc/view-set-geometry", set_view_geometry);
        ipc_server->connect(&on_client_disconnected);
        wf::get_core().connect(&on_view_mapped);
    }

    void fini() override
    {
        method_repository->unregister_method("demo-ipc/watch");
        method_repository->unregister_method("demo-ipc/view-info");
        method_repository->unregister_method("demo-ipc/output-info");
        method_repository->unregister_method("demo-ipc/view-set-geometry");
    }

    wf::ipc::method_callback on_client_watch = [=] (nlohmann::json data)
    {
        clients.insert(ipc_server->get_current_request_client());
        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback get_view_info = [=] (nlohmann::json data)
    {
        WFJSON_EXPECT_FIELD(data, "id", number_integer);

        for (auto view : wf::get_core().get_all_views())
        {
            if (view->get_id() == data["id"])
            {
                auto response = wf::ipc::json_ok();
                response["info"] = view_to_json(view);
                return response;
            }
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
        response["info"]["name"]     = wo->to_string();
        response["info"]["geometry"] = wf::ipc::geometry_to_json(wo->get_layout_geometry());
        return response;
    };

    wf::ipc::method_callback set_view_geometry = [=] (nlohmann::json data)
    {
        WFJSON_EXPECT_FIELD(data, "id", number_integer);
        WFJSON_EXPECT_FIELD(data, "geometry", object);

        if (auto view = wf::ipc::find_view_by_id(data["id"]))
        {
            if (auto geometry = wf::ipc::geometry_from_json(data["geometry"]))
            {
                if (auto toplevel = toplevel_cast(view))
                {
                    toplevel->set_geometry(geometry.value());
                    return wf::ipc::json_ok();
                }

                return wf::ipc::json_error("view is not toplevel");
            }

            return wf::ipc::json_error("geometry incorrect");
        }

        return wf::ipc::json_error("view not found");
    };

  private:
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> method_repository;
    wf::shared_data::ref_ptr_t<wf::ipc::server_t> ipc_server;
    std::set<wf::ipc::client_t*> clients;

    wf::signal::connection_t<wf::ipc::client_disconnected_signal> on_client_disconnected =
        [=] (wf::ipc::client_disconnected_signal *ev)
    {
        clients.erase(ev->client);
    };

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        nlohmann::json event;
        event["event"] = "view-mapped";
        event["view"]  = view_to_json(ev->view);
        for (auto& client : clients)
        {
            client->send_json(event);
        }
    };

    nlohmann::json view_to_json(wayfire_view view)
    {
        nlohmann::json description;
        description["id"]     = view->get_id();
        description["app-id"] = view->get_app_id();
        description["title"]  = view->get_title();
        auto toplevel = wf::toplevel_cast(view);
        description["geometry"] =
            wf::ipc::geometry_to_json(toplevel ? toplevel->get_geometry() : view->get_bounding_box());
        description["output"] = view->get_output() ? view->get_output()->get_id() : -1;
        return description;
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_demo_ipc);
