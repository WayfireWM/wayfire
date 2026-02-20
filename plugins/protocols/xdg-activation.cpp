#include "wayfire/core.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/view.hpp"
#include <memory>
#include <wayfire/plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/util.hpp>
#include "config.h"

class wayfire_xdg_activation_protocol_impl : public wf::plugin_interface_t
{
  public:
    wayfire_xdg_activation_protocol_impl()
    {
        set_callbacks();
    }

    void init() override
    {
        xdg_activation = wlr_xdg_activation_v1_create(wf::get_core().display);
        if (timeout >= 0)
        {
            xdg_activation->token_timeout_msec = 1000 * timeout;
        }

        xdg_activation_request_activate.connect(&xdg_activation->events.request_activate);
        xdg_activation_new_token.connect(&xdg_activation->events.new_token);
    }

    void fini() override
    {
        xdg_activation_request_activate.disconnect();
        xdg_activation_new_token.disconnect();
        xdg_activation_token_destroy.disconnect();
        last_token = nullptr;
        if (last_view)
        {
            last_view->disconnect(&on_view_unmapped);
            last_view->disconnect(&on_view_deactivated);
            last_view = nullptr;
        }
    }

    bool is_unloadable() override
    {
        return false;
    }

  private:
    void set_callbacks()
    {
        xdg_activation_request_activate.set_callback([this] (void *data)
        {
            auto event = static_cast<const struct wlr_xdg_activation_v1_request_activate_event*>(data);

            if (!event->token->seat)
            {
                LOGI("Denying focus request, token was rejected at creation");
                return;
            }

            if (only_last_token && (event->token != last_token))
            {
                LOGI("Denying focus request, token is expired");
                return;
            }

            last_token = nullptr; // avoid reusing the same token

            if (last_view)
            {
                last_view->disconnect(&on_view_unmapped);
                last_view->disconnect(&on_view_deactivated);
                last_view = nullptr;
            } else if (prevent_focus_stealing)
            {
                LOGI("Denying focus request, requesting view has been deactivated");
                return;
            }

            wayfire_view view = wf::wl_surface_to_wayfire_view(event->surface->resource);
            if (!view)
            {
                LOGE("Could not get view");
                return;
            }

            auto toplevel = wf::toplevel_cast(view);
            if (!toplevel)
            {
                LOGE("Could not get toplevel view");
                return;
            }

            LOGD("Activating view");
            wf::get_core().default_wm->focus_request(toplevel);
        });

        xdg_activation_new_token.set_callback([this] (void *data)
        {
            auto token = static_cast<struct wlr_xdg_activation_token_v1*>(data);
            if (!token->seat)
            {
                // note: for a valid seat, wlroots already checks that the serial is valid
                LOGI("Not registering activation token, seat was not supplied");
                return;
            }

            if (check_surface && !token->surface)
            {
                // note: for a valid surface, wlroots already checks that this is the active surface
                LOGI("Not registering activation token, surface was not supplied");
                token->seat = nullptr; // this will ensure that this token will be rejected later
                return;
            }

            // unset any previously saved view
            if (last_view)
            {
                last_view->disconnect(&on_view_unmapped);
                last_view->disconnect(&on_view_deactivated);
                last_view = nullptr;
            }

            wayfire_view view = token->surface ? wf::wl_surface_to_wayfire_view(
                token->surface->resource) : nullptr;
            if (view)
            {
                last_view = wf::toplevel_cast(view); // might return nullptr
                if (last_view)
                {
                    last_view->connect(&on_view_unmapped);
                    last_view->connect(&on_view_deactivated);
                }
            }

            // update our token and connect its destroy signal
            last_token = token;
            xdg_activation_token_destroy.disconnect();
            xdg_activation_token_destroy.connect(&token->events.destroy);
        });

        xdg_activation_token_destroy.set_callback([this] (void *data)
        {
            last_token = nullptr;

            xdg_activation_token_destroy.disconnect();
        });

        timeout.set_callback(timeout_changed);
    }

    wf::config::option_base_t::updated_callback_t timeout_changed =
        [this] ()
    {
        if (xdg_activation && (timeout >= 0))
        {
            xdg_activation->token_timeout_msec = 1000 * timeout;
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped = [this] (auto)
    {
        last_view->disconnect(&on_view_unmapped);
        last_view->disconnect(&on_view_deactivated);
        // handle the case when last_view was a dialog that is closed by user interaction
        last_view = last_view->parent;
        if (last_view)
        {
            last_view->connect(&on_view_unmapped);
            last_view->connect(&on_view_deactivated);
        }
    };

    wf::signal::connection_t<wf::view_activated_state_signal> on_view_deactivated = [this] (auto)
    {
        if (last_view->activated)
        {
            // could be a spurious event, e.g. activating the parent
            // view after closing a dialog
            return;
        }

        last_view->disconnect(&on_view_unmapped);
        last_view->disconnect(&on_view_deactivated);
        last_view = nullptr;
    };

    struct wlr_xdg_activation_v1 *xdg_activation;
    wf::wl_listener_wrapper xdg_activation_request_activate;
    wf::wl_listener_wrapper xdg_activation_new_token;
    wf::wl_listener_wrapper xdg_activation_token_destroy;
    struct wlr_xdg_activation_token_v1 *last_token = nullptr;
    wayfire_toplevel_view last_view = nullptr; // view that created the token

    wf::option_wrapper_t<bool> check_surface{"xdg-activation/check_surface"};
    wf::option_wrapper_t<bool> only_last_token{"xdg-activation/only_last_request"};
    wf::option_wrapper_t<bool> prevent_focus_stealing{"xdg-activation/focus_stealing_prevention"};
    wf::option_wrapper_t<int> timeout{"xdg-activation/timeout"};
};

DECLARE_WAYFIRE_PLUGIN(wayfire_xdg_activation_protocol_impl);
