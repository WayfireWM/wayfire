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

    struct wlr_xdg_activation_v1 *xdg_activation;
    wf::wl_listener_wrapper xdg_activation_request_activate;
    wf::wl_listener_wrapper xdg_activation_new_token;
    wf::wl_listener_wrapper xdg_activation_token_destroy;
    struct wlr_xdg_activation_token_v1 *last_token = nullptr;

    wf::option_wrapper_t<bool> check_surface{"xdg-activation/check_surface"};
    wf::option_wrapper_t<bool> only_last_token{"xdg-activation/only_last_request"};
    wf::option_wrapper_t<int> timeout{"xdg-activation/timeout"};
};

DECLARE_WAYFIRE_PLUGIN(wayfire_xdg_activation_protocol_impl);
