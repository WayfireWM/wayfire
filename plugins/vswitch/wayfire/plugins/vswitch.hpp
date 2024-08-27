#pragma once

#include "wayfire/scene-input.hpp"
#include "wayfire/seat.hpp"
#include "wayfire/core.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/view-helpers.hpp"
#include <memory>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>
#include <wayfire/plugins/common/workspace-wall.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/config/compound-option.hpp>
#include <wayfire/view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/nonstd/reverse.hpp>
#include <cmath>

namespace wf
{
namespace vswitch
{
/**
 * A simple class to register the vswitch bindings and get a custom callback called.
 */
class control_bindings_t
{
  public:
    /**
     * Create a vswitch binding instance for the given output.
     *
     * The bindings will not be automatically connected.
     */
    control_bindings_t(wf::output_t *output)
    {
        this->output = output;

        workspace_bindings.set_callback(on_cfg_reload);
        workspace_bindings_win.set_callback(on_cfg_reload);
        bindings_win.set_callback(on_cfg_reload);
    }

    virtual ~control_bindings_t()
    {
        tear_down();
    }

    control_bindings_t(const control_bindings_t &) = delete;
    control_bindings_t(control_bindings_t &&) = delete;
    control_bindings_t& operator =(const control_bindings_t&) = delete;
    control_bindings_t& operator =(control_bindings_t&&) = delete;

    /**
     * A binding callback for vswitch.
     *
     * @param delta The difference between current and target workspace.
     * @param view The view to be moved together with the switch, or nullptr.
     * @param window_only Move only the view to the given workspace. It is
     *   guaranteed that @view will not be nullptr if this is true.
     */
    using binding_callback_t = std::function<bool (
        wf::point_t delta, wayfire_toplevel_view view, bool window_only)>;

    /**
     * Connect bindings on the output.
     *
     * @param callback The callback to execute on each binding
     */
    void setup(binding_callback_t callback)
    {
        tear_down();
        this->user_cb = callback;

        // Setup a new binding on the output.
        //
        // @param name The binding name
        // @param dx, dy The delta from the current workspace
        // @param only Whether to grab the current view
        // @param only Whether to move the view without changing workspaces
        /* *INDENT-OFF* */ // Uncrustify has problems with this macro
#define SETUP(name, dx, dy, view, only) \
        wf::option_wrapper_t<wf::activatorbinding_t> binding_##name { \
            "vswitch/"#name}; \
        activator_cbs.push_back(std::make_unique<activator_callback>()); \
        *activator_cbs.back() = [=] (const wf::activator_data_t&) \
        {\
            return handle_dir({dx, dy}, view, only, callback); \
        };\
        output->add_activator(binding_##name, activator_cbs.back().get());

        // Setup bindings for the 4 directions (left/right/up/down)
#define SETUP4(prefix, view, only) \
        SETUP(prefix ## _left, -1, 0, view, only) \
        SETUP(prefix ## _right, 1, 0, view, only) \
        SETUP(prefix ## _up, 0, -1, view, only) \
        SETUP(prefix ## _down, 0, 1, view, only)

        SETUP4(binding, nullptr, false);
        SETUP4(with_win, get_target_view(), false);
        SETUP4(send_win, get_target_view(), true);

#undef SETUP4
#undef SETUP
        /* *INDENT-ON* */

        // Setup the bindings for switching back to the last workspace for the output
        /* *INDENT-OFF* */ // Uncrustify has problems with this macro
#define SETUP_LAST(name, view, only) \
        wf::option_wrapper_t<wf::activatorbinding_t> binding_##name { \
            "vswitch/"#name}; \
        activator_cbs.push_back(std::make_unique<activator_callback>()); \
        *activator_cbs.back() = [=] (const wf::activator_data_t&) \
        {\
            return handle_dir(-get_last_dir(), view, only, callback); \
        };\
        output->add_activator(binding_##name, activator_cbs.back().get());

        SETUP_LAST(binding_last, nullptr, false);
        SETUP_LAST(with_win_last, get_target_view(), false);
        SETUP_LAST(send_win_last, get_target_view(), true);
#undef SETUP_LAST
        /* *INDENT-ON* */

        // Setup a binding for going directly to a workspace identified by a name
        const auto& setup_direct_binding = [=] (wf::activatorbinding_t binding,
                                                std::string workspace_name,
                                                bool grab_view, bool only_view)
        {
            auto ws_nr = wf::option_type::from_string<int>(workspace_name);
            if (!ws_nr)
            {
                LOGE("Invalid vswitch binding, no such workspace ", workspace_name);
                return;
            }

            int nr = ws_nr.value();
            --nr;

            activator_cbs.push_back(std::make_unique<activator_callback>());
            *activator_cbs.back() = [=] (const wf::activator_data_t&)
            {
                auto [wsize, hsize] = output->wset()->get_workspace_grid_size();

                // Calculate target x,y each time.
                // Workspace grid size might change at runtime, so we cannot
                // compute them at initialization time
                wf::point_t target{
                    nr % wsize,
                    nr / wsize,
                };
                wf::point_t current = output->wset()->get_current_workspace();

                auto view = (grab_view ? get_target_view() : nullptr);
                return handle_dir(target - current, view, only_view, callback);
            };

            output->add_activator(wf::create_option(binding),
                activator_cbs.back().get());
        };

        for (auto& [name, act] : workspace_bindings.value())
        {
            setup_direct_binding(act, name, false, false);
        }

        for (auto& [name, act] : workspace_bindings_win.value())
        {
            setup_direct_binding(act, name, true, false);
        }

        for (auto& [name, act] : bindings_win.value())
        {
            setup_direct_binding(act, name, true, true);
        }
    }

    /**
     * Disconnect the bindings.
     */
    void tear_down()
    {
        for (auto& cb : activator_cbs)
        {
            output->rem_binding(cb.get());
        }

        activator_cbs.clear();
    }

  protected:
    binding_callback_t user_cb;
    std::vector<std::unique_ptr<wf::activator_callback>> activator_cbs;

    wf::point_t last_dir = {0, 0};

    wf::wl_idle_call idle_reload;
    wf::config::option_base_t::updated_callback_t on_cfg_reload = [=] ()
    {
        // Aggregate multiple updates together
        idle_reload.run_once([=] ()
        {
            // Reload only if the plugin has already setup bindings once,
            // otherwise, we do not have any callbacks to register.
            if (user_cb)
            {
                setup(user_cb);
            }
        });
    };

    wf::option_wrapper_t<wf::config::compound_list_t<wf::activatorbinding_t>>
    workspace_bindings{"vswitch/workspace_bindings"};
    wf::option_wrapper_t<wf::config::compound_list_t<wf::activatorbinding_t>>
    workspace_bindings_win{"vswitch/workspace_bindings_win"};
    wf::option_wrapper_t<wf::config::compound_list_t<wf::activatorbinding_t>>
    bindings_win{"vswitch/bindings_win"};

    wf::option_wrapper_t<bool> wraparound{"vswitch/wraparound"};

    wf::output_t *output;

    /** Find the view to switch workspace with */
    virtual wayfire_toplevel_view get_target_view()
    {
        auto view = find_topmost_parent(toplevel_cast(wf::get_core().seat->get_active_view()));
        if (!view || (view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            return nullptr;
        }

        return view;
    }

    virtual wf::point_t get_last_dir()
    {
        return this->last_dir;
    }

    /**
     * Handle binding in the given direction. The next workspace will be
     * determined by the current workspace, target direction and wraparound
     * mode.
     */
    virtual bool handle_dir(wf::point_t dir, wayfire_toplevel_view view, bool window_only,
        binding_callback_t callback)
    {
        if (!view && window_only)
        {
            // Maybe there is no view, in any case, no need to do anything
            return false;
        }

        auto ws = output->wset()->get_current_workspace();
        auto target_ws = ws + dir;
        if (!output->wset()->is_workspace_valid(target_ws))
        {
            if (wraparound)
            {
                auto grid_size = output->wset()->get_workspace_grid_size();
                target_ws.x = (target_ws.x + grid_size.width) % grid_size.width;
                target_ws.y = (target_ws.y + grid_size.height) % grid_size.height;
            } else
            {
                target_ws = ws;
            }
        }

        // Remember the direction we are moving now so that we can potentially
        // move back. Only remember when we are actually changing the workspace
        // and not just move a view around.
        if (!window_only)
        {
            if (target_ws != ws)
            {
                this->last_dir = target_ws - ws;
            }
        }

        return callback(target_ws - ws, view, window_only);
    }
};
}
}
