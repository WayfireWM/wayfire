#include <wayfire/singleton-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-manager.hpp>
#include <getopt.h>

#include "ipc.hpp"

namespace wf
{
static nlohmann::json geometry_to_json(wf::geometry_t g)
{
    nlohmann::json j;
    j["x"]     = g.x;
    j["y"]     = g.y;
    j["width"] = g.width;
    j["height"] = g.height;
    return j;
}

static std::string layer_to_string(uint32_t layer)
{
    switch (layer)
    {
      case LAYER_BACKGROUND:
        return "background";

      case LAYER_BOTTOM:
        return "bottom";

      case LAYER_WORKSPACE:
        return "workspace";

      case LAYER_TOP:
        return "top";

      case LAYER_UNMANAGED:
        return "unmanaged";

      case LAYER_LOCK:
        return "lock";

      case LAYER_DESKTOP_WIDGET:
        return "dew";

      case LAYER_MINIMIZED:
        return "minimized";

      default:
        break;
    }

    return "none";
}

class ipc_plugin_t
{
  public:
    ipc_plugin_t()
    {
        char *pre_socket   = getenv("_WAYFIRE_SOCKET");
        const auto& dname  = wf::get_core().wayland_display;
        std::string socket = pre_socket ?: "/tmp/wayfire-" + dname + ".socket";
        setenv("WAYFIRE_SOCKET", socket.c_str(), 1);

        server = std::make_unique<ipc::server_t>(socket);
        server->register_method("core/list_views", list_views);
    }

    using method_t = ipc::server_t::method_cb;

    method_t list_views = [] (nlohmann::json)
    {
        auto response = nlohmann::json::array();

        for (auto& view : wf::get_core().get_all_views())
        {
            nlohmann::json v;
            v["title"]    = view->get_title();
            v["app-id"]   = view->get_app_id();
            v["geometry"] = geometry_to_json(view->get_wm_geometry());
            v["base-geometry"] = geometry_to_json(view->get_output_geometry());
            v["state"] = {
                {"tiled", view->tiled_edges},
                {"fullscreen", view->fullscreen},
                {"minimized", view->minimized},
            };

            auto layer = view->get_output()->workspace->get_view_layer(view);
            v["layer"] = layer_to_string(layer);

            response.push_back(v);
        }

        return response;
    };

    std::unique_ptr<ipc::server_t> server;
};
}

DECLARE_WAYFIRE_PLUGIN((wf::singleton_plugin_t<wf::ipc_plugin_t, false>));
