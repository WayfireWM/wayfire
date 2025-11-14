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

class wayfire_foreign_toplevel;
using foreign_toplevel_map_type = std::map<wayfire_toplevel_view, std::unique_ptr<wayfire_foreign_toplevel>>;

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

  public:
    wayfire_ext_foreign_toplevel(wayfire_toplevel_view view, wlr_ext_foreign_toplevel_handle_v1 *hndl) :
        view(view),
        handle(hndl)
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
        // No request handlers at the present moment.
        // We may want to deal with them later on while implementing
        // ext-foreign-toplevel-management protocol.
    }

    virtual void send_initial_state()
    {
        toplevel_send_state();
    }

    virtual void init_connections()
    {
        view->connect(&on_title_changed);
        view->connect(&on_app_id_changed);
    }

    virtual void disconnect_request_handlers()
    {
        // No request handlers at the present moment.
        // We may want to deal with them later on while implementing
        // ext-foreign-toplevel-management protocol.
    }

    virtual void destroy_handle()
    {
        wlr_ext_foreign_toplevel_handle_v1_destroy(handle);
    }

    virtual void toplevel_send_state()
    {
        // Prepare the state
        struct wlr_ext_foreign_toplevel_handle_v1_state new_state;
        get_state(view, &new_state);

        /** Send the state; done() is sent by wlroots */
        wlr_ext_foreign_toplevel_handle_v1_update_state(handle,
            &new_state);
    }

    wf::signal::connection_t<wf::view_title_changed_signal> on_title_changed = [=] (auto)
    {
        toplevel_send_state();
    };

    wf::signal::connection_t<wf::view_app_id_changed_signal> on_app_id_changed = [=] (auto)
    {
        toplevel_send_state();
    };
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

            handle_for_view[toplevel] = std::make_unique<wayfire_ext_foreign_toplevel>(toplevel, handle);
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        handle_for_view.erase(toplevel_cast(ev->view));
    };

    wlr_ext_foreign_toplevel_list_v1 *toplevel_manager;
    std::map<wayfire_toplevel_view, std::unique_ptr<wayfire_ext_foreign_toplevel>> handle_for_view;
};

DECLARE_WAYFIRE_PLUGIN(wayfire_ext_foreign_toplevel_protocol_impl);
