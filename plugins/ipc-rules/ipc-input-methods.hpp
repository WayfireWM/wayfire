#pragma once
#include "plugins/ipc/ipc-helpers.hpp"
#include "plugins/ipc/ipc-method-repository.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include <wayfire/input-device.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

namespace wf
{
class ipc_rules_input_methods_t
{
  public:
    void init_input_methods(ipc::method_repository_t *method_repository)
    {
        method_repository->register_method("input/list-devices", list_input_devices);
        method_repository->register_method("input/configure-device", configure_input_device);
    }

    void fini_input_methods(ipc::method_repository_t *method_repository)
    {
        method_repository->unregister_method("input/list-devices");
        method_repository->unregister_method("input/configure-device");
    }

    static std::string wlr_input_device_type_to_string(wlr_input_device_type type)
    {
        switch (type)
        {
          case WLR_INPUT_DEVICE_KEYBOARD:
            return "keyboard";

          case WLR_INPUT_DEVICE_POINTER:
            return "pointer";

          case WLR_INPUT_DEVICE_TOUCH:
            return "touch";

          case WLR_INPUT_DEVICE_TABLET_TOOL:
            return "tablet_tool";

          case WLR_INPUT_DEVICE_TABLET_PAD:
            return "tablet_pad";

          case WLR_INPUT_DEVICE_SWITCH:
            return "switch";

          default:
            return "unknown";
        }
    }

    wf::ipc::method_callback list_input_devices = [&] (const Json::Value&)
    {
        Json::Value response = Json::arrayValue;
        for (auto& device : wf::get_core().get_input_devices())
        {
            Json::Value d;
            d["id"]     = (intptr_t)device->get_wlr_handle();
            d["name"]   = nonull(device->get_wlr_handle()->name);
            d["vendor"] = device->get_wlr_handle()->vendor;
            d["product"] = device->get_wlr_handle()->product;
            d["type"]    = wlr_input_device_type_to_string(device->get_wlr_handle()->type);
            d["enabled"] = device->is_enabled();
            response.append(d);
        }

        return response;
    };

    wf::ipc::method_callback configure_input_device = [&] (const Json::Value& data)
    {
        auto id = wf::ipc::json_get_int64(data, "id");
        auto enabled = wf::ipc::json_get_bool(data, "enabled");

        for (auto& device : wf::get_core().get_input_devices())
        {
            if ((intptr_t)device->get_wlr_handle() == id)
            {
                device->set_enabled(enabled);

                return wf::ipc::json_ok();
            }
        }

        return wf::ipc::json_error("Unknown input device!");
    };
};
}
