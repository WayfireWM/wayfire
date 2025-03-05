#pragma once

#include <wayfire/bindings-repository.hpp>
#include <wayfire/config/types.hpp>
#include "ipc-helpers.hpp"
#include "ipc-method-repository.hpp"
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

    ipc::method_callback ipc_cb = [=] (const wf::json_t& data)
    {
        auto output_id = ipc::json_get_optional_int64(data, "output_id");
        if (!output_id.has_value())
        {
            output_id = ipc::json_get_optional_int64(data, "output-id");
        }

        auto view_id = ipc::json_get_optional_int64(data, "view_id");
        if (!view_id.has_value())
        {
            view_id = ipc::json_get_optional_int64(data, "view-id");
        }

        wf::output_t *wo = wf::get_core().seat->get_active_output();
        if (output_id.has_value())
        {
            wo = ipc::find_output_by_id(output_id.value());
            if (!wo)
            {
                return ipc::json_error("output id not found!");
            }
        }

        wayfire_view view;
        if (view_id.has_value())
        {
            view = ipc::find_view_by_id(view_id.has_value());
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
