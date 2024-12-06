#include "wayfire/core.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/view.hpp"
#include <memory>
#include <wayfire/plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/window-manager.hpp>
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

            LOGI("Activating view");
            wf::get_core().default_wm->focus_request(toplevel);
        });

        xdg_activation_new_token.set_callback([this] (void *data)
        {
            auto token = static_cast<struct wlr_xdg_activation_token_v1*>(data);
            bool reject_token = false;
            if (!token->seat)
            {
                // note: for a valid seat, wlroots already checks that the serial is valid
                LOGI("Not registering activation token, seat was not supplied");
                reject_token = true;
            }

            if (check_surface && !token->surface)
            {
                // note: for a valid surface, wlroots already checks that this is the active surface
                LOGI("Not registering activation token, surface was not supplied");
                token->seat  = nullptr; // this will ensure that this token will be rejected later
                reject_token = true;
            }

            if (reject_token)
            {
                if (token == last_token)
                {
                    /* corner case: (1) we created a valid token, storing it in last_token (2) it was freed by
                     * wlroots without using it (3) a new token is created that is allocated the same memory.
                     * In this case, we explicitly mark it as invalid. In other cases, we do not touch
                     * last_token, since it can be a valid one and we don't want to cancel it because of a
                     * rejected request.
                     */
                    last_token = nullptr;
                }

                // we will reject this token also in the activate callback
                return;
            }

            last_token = token; // update our token
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
    /* last valid token generated -- might be stale if it was destroyed by wlroots, should not be
     * dereferenced, only compared to other tokens */
    struct wlr_xdg_activation_token_v1 *last_token = nullptr;

    wf::option_wrapper_t<bool> check_surface{"xdg-activation/check_surface"};
    wf::option_wrapper_t<bool> only_last_token{"xdg-activation/only_last_request"};
    wf::option_wrapper_t<int> timeout{"xdg-activation/timeout"};
};

DECLARE_WAYFIRE_PLUGIN(wayfire_xdg_activation_protocol_impl);
