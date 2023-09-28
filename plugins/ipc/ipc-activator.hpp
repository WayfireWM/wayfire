#pragma once

#include <wayfire/bindings-repository.hpp>
#include <wayfire/config/types.hpp>
#include "ipc-helpers.hpp"
#include "plugins/ipc/ipc-method-repository.hpp"
#include "wayfire/bindings.hpp"
#include "wayfire/core.hpp"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/output.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include "wayfire/seat.hpp"

namespace wf
{
/**
 * The IPC activator class is a helper class which combines an IPC method with a normal activator binding.
 */

class ipc_activator_t
{
  public:
    ipc_activator_t()
    {}

    ipc_activator_t(std::string name)
    {
        load_from_xml_option(name);
    }

    void load_from_xml_option(std::string name)
    {
        activator.load_option(name);
        wf::get_core().bindings->add_activator(activator, &activator_cb);
        repo->register_method(name, ipc_cb);
        this->name = name;
    }

    ~ipc_activator_t()
    {
        wf::get_core().bindings->rem_binding(&activator_cb);
        repo->unregister_method(name);
    }

    /**
     * The handler is given over an optional output and a view to execute the action for.
     * Note that the output is always set (if not explicitly given, then it is set to the currently focused
     * output), however the view might be nullptr if not indicated in the IPC call or in the case of
     * activators, no suitable view could be found for the cursor/keyboard focus.
     */
    using handler_t = std::function<bool (wf::output_t*, wayfire_view)>;
    void set_handler(handler_t hnd)
    {
        this->hnd = hnd;
    }

  private:
    wf::option_wrapper_t<activatorbinding_t> activator;
    shared_data::ref_ptr_t<ipc::method_repository_t> repo;
    std::string name;
    handler_t hnd;

    activator_callback activator_cb = [=] (const wf::activator_data_t& data) -> bool
    {
        if (hnd)
        {
            return hnd(choose_output(), choose_view(data.source));
        }

        return false;
    };

    ipc::method_callback ipc_cb = [=] (const nlohmann::json& data)
    {
        WFJSON_OPTIONAL_FIELD(data, "output_id", number_integer);
        WFJSON_OPTIONAL_FIELD(data, "view_id", number_integer);

        wf::output_t *wo = wf::get_core().seat->get_active_output();
        if (data.contains("output_id"))
        {
            wo = ipc::find_output_by_id(data["output_id"]);
            if (!wo)
            {
                return ipc::json_error("output id not found!");
            }
        }

        wayfire_view view;
        if (data.contains("view_id"))
        {
            view = ipc::find_view_by_id(data["view_id"]);
            if (!view)
            {
                return ipc::json_error("view id not found!");
            }
        }

        if (hnd)
        {
            hnd(wo, view);
        }

        return ipc::json_ok();
    };

    wf::output_t *choose_output()
    {
        return wf::get_core().seat->get_active_output();
    }

    wayfire_view choose_view(wf::activator_source_t source)
    {
        wayfire_view view;
        if (source == wf::activator_source_t::BUTTONBINDING)
        {
            view = wf::get_core().get_cursor_focus_view();
        } else
        {
            view = wf::get_core().seat->get_active_view();
        }

        return view;
    }
};
}
