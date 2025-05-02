#include <wayfire/plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/seat.hpp>

#include "wayfire/plugins/ipc/ipc-helpers.hpp"
#include "wayfire/plugins/ipc/ipc-method-repository.hpp"
#include "wayfire/core.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include "wayfire/window-manager.hpp"
#include <wayfire/debug.hpp>
#include <wayfire/signal-definitions.hpp>

#include "ipc-rules-common.hpp"
#include "ipc-input-methods.hpp"
#include "ipc-utility-methods.hpp"
#include "ipc-events.hpp"

class ipc_rules_t : public wf::plugin_interface_t,
    public wf::ipc_rules_input_methods_t,
    public wf::ipc_rules_utility_methods_t,
    public wf::ipc_rules_events_methods_t
{
  public:
    void init() override
    {
        method_repository->register_method("window-rules/list-views", list_views);
        method_repository->register_method("window-rules/list-outputs", list_outputs);
        method_repository->register_method("window-rules/list-wsets", list_wsets);
        method_repository->register_method("window-rules/view-info", get_view_info);
        method_repository->register_method("window-rules/output-info", get_output_info);
        method_repository->register_method("window-rules/wset-info", get_wset_info);
        method_repository->register_method("window-rules/get_cursor_position", get_cursor_position);
        method_repository->register_method("window-rules/configure-view", configure_view);
        method_repository->register_method("window-rules/focus-view", focus_view);
        method_repository->register_method("window-rules/get-focused-view", get_focused_view);
        method_repository->register_method("window-rules/get-focused-output", get_focused_output);
        method_repository->register_method("window-rules/close-view", close_view);

        init_input_methods(method_repository.get());
        init_utility_methods(method_repository.get());
        init_events(method_repository.get());
    }

    void fini() override
    {
        method_repository->unregister_method("window-rules/list-views");
        method_repository->unregister_method("window-rules/list-outputs");
        method_repository->unregister_method("window-rules/list-wsets");
        method_repository->unregister_method("window-rules/view-info");
        method_repository->unregister_method("window-rules/output-info");
        method_repository->unregister_method("window-rules/wset-info");
        method_repository->unregister_method("window-rules/configure-view");
        method_repository->unregister_method("window-rules/focus-view");
        method_repository->unregister_method("window-rules/get-focused-view");
        method_repository->unregister_method("window-rules/get-focused-output");
        method_repository->unregister_method("window-rules/get-cursor-position");
        method_repository->unregister_method("window-rules/close-view");

        fini_input_methods(method_repository.get());
        fini_utility_methods(method_repository.get());
        fini_events(method_repository.get());
    }

    wf::ipc::method_callback list_views = [=] (wf::json_t)
    {
        wf::json_t response = wf::json_t::array();
        for (auto& view : wf::get_core().get_all_views())
        {
            wf::json_t v = view_to_json(view);
            response.append(v);
        }

        return response;
    };

    wf::ipc::method_callback get_view_info = [=] (wf::json_t data)
    {
        auto id = wf::ipc::json_get_uint64(data, "id");
        if (auto view = wf::ipc::find_view_by_id(id))
        {
            auto response = wf::ipc::json_ok();
            response["info"] = view_to_json(view);
            return response;
        }

        return wf::ipc::json_error("no such view");
    };

    wf::ipc::method_callback get_focused_view = [=] (wf::json_t data)
    {
        if (auto view = wf::get_core().seat->get_active_view())
        {
            auto response = wf::ipc::json_ok();
            response["info"] = view_to_json(view);
            return response;
        } else
        {
            auto response = wf::ipc::json_ok();
            response["info"] = wf::json_t::null();
            return response;
        }
    };

    wf::ipc::method_callback get_focused_output = [=] (wf::json_t data)
    {
        auto active_output = wf::get_core().seat->get_active_output();
        auto response = wf::ipc::json_ok();

        if (active_output)
        {
            response["info"] = output_to_json(active_output);
        } else
        {
            response["info"] = wf::json_t::null();
        }

        return response;
    };

    wf::ipc::method_callback focus_view = [=] (wf::json_t data)
    {
        auto id = wf::ipc::json_get_uint64(data, "id");
        if (auto view = wf::ipc::find_view_by_id(id))
        {
            auto response = wf::ipc::json_ok();
            auto toplevel = wf::toplevel_cast(view);
            if (!toplevel)
            {
                return wf::ipc::json_error("view is not toplevel");
            }

            wf::get_core().default_wm->focus_request(toplevel);
            return response;
        }

        return wf::ipc::json_error("no such view");
    };

    wf::ipc::method_callback close_view = [=] (wf::json_t data)
    {
        auto id = wf::ipc::json_get_uint64(data, "id");
        if (auto view = wf::ipc::find_view_by_id(id))
        {
            auto response = wf::ipc::json_ok();
            view->close();
            return response;
        }

        return wf::ipc::json_error("no such view");
    };

    wf::ipc::method_callback list_outputs = [=] (wf::json_t)
    {
        wf::json_t response = wf::json_t::array();
        for (auto& output : wf::get_core().output_layout->get_outputs())
        {
            response.append(output_to_json(output));
        }

        return response;
    };

    wf::ipc::method_callback get_output_info = [=] (wf::json_t data)
    {
        auto id = wf::ipc::json_get_uint64(data, "id");
        auto wo = wf::ipc::find_output_by_id(id);
        if (!wo)
        {
            return wf::ipc::json_error("output not found");
        }

        auto response = output_to_json(wo);
        return response;
    };

    wf::ipc::method_callback configure_view = [=] (wf::json_t data)
    {
        auto id = wf::ipc::json_get_uint64(data, "id");
        auto output_id = wf::ipc::json_get_optional_uint64(data, "output_id");

        if (data.has_member("geometry") && !data["geometry"].is_object())
        {
            return wf::ipc::json_error("invalid geometry");
        }

        auto sticky = wf::ipc::json_get_optional_bool(data, "sticky");
        auto view   = wf::ipc::find_view_by_id(id);
        if (!view)
        {
            return wf::ipc::json_error("view not found");
        }

        auto toplevel = wf::toplevel_cast(view);
        if (!toplevel)
        {
            return wf::ipc::json_error("view is not toplevel");
        }

        if (output_id.has_value())
        {
            auto wo = wf::ipc::find_output_by_id(output_id.value());
            if (!wo)
            {
                return wf::ipc::json_error("output not found");
            }

            wf::move_view_to_output(toplevel, wo, !data.has_member("geometry"));
        }

        if (data.has_member("geometry"))
        {
            auto geometry = wf::ipc::geometry_from_json(data["geometry"]);
            if (!geometry)
            {
                return wf::ipc::json_error("invalid geometry");
            }

            toplevel->set_geometry(*geometry);
        }

        if (sticky.has_value())
        {
            toplevel->set_sticky(sticky.value());
        }

        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback list_wsets = [=] (wf::json_t)
    {
        wf::json_t response = wf::json_t::array();
        for (auto& workspace_set : wf::workspace_set_t::get_all())
        {
            response.append(wset_to_json(workspace_set.get()));
        }

        return response;
    };

    wf::ipc::method_callback get_wset_info = [=] (wf::json_t data)
    {
        auto id = wf::ipc::json_get_uint64(data, "id");
        auto ws = wf::ipc::find_workspace_set_by_index(id);
        if (!ws)
        {
            return wf::ipc::json_error("workspace set not found");
        }

        auto response = wset_to_json(ws);
        return response;
    };

    wf::ipc::method_callback get_cursor_position = [=] (wf::json_t data) -> wf::json_t
    {
        wf::json_t response = wf::ipc::json_ok();
        auto cursor = wf::get_core().get_cursor_position();
        response["pos"]["x"] = cursor.x;
        response["pos"]["y"] = cursor.y;
        return response;
    };

  private:
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> method_repository;
};

DECLARE_WAYFIRE_PLUGIN(ipc_rules_t);
