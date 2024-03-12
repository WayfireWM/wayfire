#pragma once

#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/seat.hpp"
#include "wayfire/core.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/unstable/translation-node.hpp"
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
using namespace animation;
class workspace_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t dx{*this};
    timed_transition_t dy{*this};
};

/**
 * A simple scenegraph node which draws a view at a fixed position and as an overlay over the workspace wall.
 */
class vswitch_overlay_node_t : public wf::scene::node_t
{
    std::weak_ptr<wf::toplevel_view_interface_t> _view;

  public:
    vswitch_overlay_node_t(wayfire_toplevel_view view) : node_t(true)
    {
        _view = view->weak_from_this();
    }

    // Since we do not grab focus via a grab node, route focus to the view being rendered as an overlay
    wf::keyboard_focus_node_t keyboard_refocus(wf::output_t *output)
    {
        if (auto view = _view.lock())
        {
            return view->get_transformed_node()->keyboard_refocus(output);
        }

        return wf::keyboard_focus_node_t{};
    }

    virtual void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
        scene::damage_callback push_damage, wf::output_t *output = nullptr)
    {
        if (auto view = _view.lock())
        {
            view->get_transformed_node()->gen_render_instances(instances, push_damage, output);
        }
    }

    virtual wf::geometry_t get_bounding_box()
    {
        if (auto view = _view.lock())
        {
            return view->get_transformed_node()->get_bounding_box();
        }

        return {0, 0, 0, 0};
    }
};

/**
 * Represents the action of switching workspaces with the vswitch algorithm.
 *
 * The workspace is actually switched at the end of the animation
 */
class workspace_switch_t
{
  public:
    /**
     * Initialize the workspace switch process.
     *
     * @param output The output the workspace switch happens on.
     */
    workspace_switch_t(output_t *output)
    {
        this->output = output;
        wall = std::make_unique<workspace_wall_t>(output);
        animation = workspace_animation_t{
            wf::option_wrapper_t<wf::animation_description_t>{"vswitch/duration"}
        };
    }

    /**
     * Initialize switching animation.
     * At this point, the calling plugin needs to have the custom renderer
     * ability set.
     */
    virtual void start_switch()
    {
        /* Setup wall */
        wall->set_gap_size(gap);
        wall->set_viewport(wall->get_workspace_rectangle(
            output->wset()->get_current_workspace()));
        wall->set_background_color(background_color);
        wall->start_output_renderer();

        if (overlay_view_node)
        {
            wf::scene::readd_front(wf::get_core().scene(), overlay_view_node);
        }

        output->render->add_effect(&post_render, OUTPUT_EFFECT_POST);

        running = true;

        /* Setup animation */
        animation.dx.set(0, 0);
        animation.dy.set(0, 0);
        animation.start();
    }

    /**
     * Start workspace switch animation towards the given workspace,
     * and set that workspace as current.
     *
     * @param workspace The new target workspace.
     */
    virtual void set_target_workspace(point_t workspace)
    {
        point_t cws = output->wset()->get_current_workspace();

        animation.dx.set(animation.dx + cws.x - workspace.x, 0);
        animation.dy.set(animation.dy + cws.y - workspace.y, 0);
        animation.start();

        std::vector<wayfire_toplevel_view> fixed_views;
        if (overlay_view)
        {
            fixed_views.push_back(overlay_view);
        }

        output->wset()->set_workspace(workspace, fixed_views);
    }

    /**
     * Set the overlay view. It will be hidden from the normal workspace layers
     * and shown on top of the workspace wall. The overlay view's position is
     * not animated together with the workspace transition, but its alpha is.
     *
     * Note: if the view disappears, the caller is responsible for resetting the
     * overlay view.
     *
     * @param view The desired overlay view, or NULL if the overlay view needs
     *   to be unset.
     */
    virtual void set_overlay_view(wayfire_toplevel_view view)
    {
        if (this->overlay_view == view)
        {
            /* Nothing to do */
            return;
        }

        /* Reset old view */
        if (this->overlay_view)
        {
            wf::scene::set_node_enabled(overlay_view->get_transformed_node(), true);
            overlay_view->get_transformed_node()->rem_transformer(
                vswitch_view_transformer_name);

            wf::scene::remove_child(overlay_view_node);
            overlay_view_node.reset();
        }

        /* Set new view */
        this->overlay_view = view;
        if (view)
        {
            view->get_transformed_node()->add_transformer(
                std::make_shared<wf::scene::view_2d_transformer_t>(view),
                wf::TRANSFORMER_2D, vswitch_view_transformer_name);
            wf::scene::set_node_enabled(view->get_transformed_node(), false);

            // Render as an overlay, but make sure it is translated to the local output
            auto vswitch_overlay = std::make_shared<vswitch_overlay_node_t>(view);

            overlay_view_node = std::make_shared<scene::translation_node_t>();
            overlay_view_node->set_children_list({vswitch_overlay});
            overlay_view_node->set_offset(origin(output->get_layout_geometry()));

            wf::scene::add_front(wf::get_core().scene(), overlay_view_node);
        }
    }

    /** @return the current overlay view, might be NULL. */
    virtual wayfire_view get_overlay_view()
    {
        return this->overlay_view;
    }

    /**
     * Called automatically when the workspace switch animation is done.
     * By default, this stops the animation.
     *
     * @param normal_exit Whether the operation has ended because of animation
     *   running out, in which case the workspace and the overlay view are
     *   adjusted, and otherwise not.
     */
    virtual void stop_switch(bool normal_exit)
    {
        if (normal_exit)
        {
            auto old_ws = output->wset()->get_current_workspace();
            adjust_overlay_view_switch_done(old_ws);
        }

        wall->stop_output_renderer(true);
        output->render->rem_effect(&post_render);
        running = false;
    }

    virtual bool is_running() const
    {
        return running;
    }

    virtual ~workspace_switch_t()
    {}

  protected:
    option_wrapper_t<int> gap{"vswitch/gap"};
    option_wrapper_t<color_t> background_color{"vswitch/background"};
    workspace_animation_t animation;

    output_t *output;
    std::unique_ptr<workspace_wall_t> wall;

    const std::string vswitch_view_transformer_name = "vswitch-transformer";
    wayfire_toplevel_view overlay_view;
    std::shared_ptr<scene::translation_node_t> overlay_view_node;

    bool running = false;
    void update_overlay_fb()
    {
        if (!overlay_view)
        {
            return;
        }

        double progress = animation.progress();

        auto tmanager = overlay_view->get_transformed_node();
        auto tr = tmanager->get_transformer<wf::scene::view_2d_transformer_t>(
            vswitch_view_transformer_name);

        static constexpr double smoothing_in     = 0.4;
        static constexpr double smoothing_out    = 0.2;
        static constexpr double smoothing_amount = 0.5;

        tmanager->begin_transform_update();
        if (progress <= smoothing_in)
        {
            tr->alpha = 1.0 - (smoothing_amount / smoothing_in) * progress;
        } else if (progress >= 1.0 - smoothing_out)
        {
            tr->alpha = 1.0 - (smoothing_amount / smoothing_out) * (1.0 - progress);
        } else
        {
            tr->alpha = smoothing_amount;
        }

        tmanager->end_transform_update();
    }

    wf::effect_hook_t post_render = [=] ()
    {
        auto start = wall->get_workspace_rectangle(
            output->wset()->get_current_workspace());
        auto size = output->get_screen_size();
        geometry_t viewport = {
            (int)std::round(animation.dx * (size.width + gap) + start.x),
            (int)std::round(animation.dy * (size.height + gap) + start.y),
            start.width,
            start.height,
        };
        wall->set_viewport(viewport);
        update_overlay_fb();

        output->render->damage_whole();
        output->render->schedule_redraw();
        if (!animation.running())
        {
            stop_switch(true);
        }
    };

    /**
     * Emit the view-change-workspace signal from the old workspace to the current
     * workspace and unset the view.
     */
    virtual void adjust_overlay_view_switch_done(wf::point_t old_workspace)
    {
        if (!overlay_view)
        {
            return;
        }

        wf::view_change_workspace_signal data;
        data.view = overlay_view;
        data.from = old_workspace;
        data.to   = output->wset()->get_current_workspace();
        output->emit(&data);

        set_overlay_view(nullptr);
        wf::get_core().seat->refocus();
    }
};

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
