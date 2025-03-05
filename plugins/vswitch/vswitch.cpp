#include "plugins/ipc/ipc-helpers.hpp"
#include "wayfire/core.hpp"
#include <wayfire/plugins/vswitch.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <linux/input.h>
#include <wayfire/util/log.hpp>
#include <wayfire/seat.hpp>
#include "plugins/ipc/ipc-method-repository.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include "wayfire/unstable/translation-node.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/render-manager.hpp"

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
}
}

class vswitch : public wf::per_output_plugin_instance_t
{
  private:

    /**
     * Adapter around the general algorithm, so that our own stop function is
     * called.
     */
    class vswitch_basic_plugin : public wf::vswitch::workspace_switch_t
    {
      public:
        vswitch_basic_plugin(wf::output_t *output,
            std::function<void()> on_done) : workspace_switch_t(output)
        {
            this->on_done = on_done;
        }

        void stop_switch(bool normal_exit) override
        {
            workspace_switch_t::stop_switch(normal_exit);
            on_done();
        }

      private:
        std::function<void()> on_done;
    };

    std::unique_ptr<vswitch_basic_plugin> algorithm;
    std::unique_ptr<wf::vswitch::control_bindings_t> bindings;

    // Capabilities which are always required for vswitch, for now wall needs
    // a custom renderer.
    static constexpr uint32_t base_caps = wf::CAPABILITY_CUSTOM_RENDERER;

    wf::plugin_activation_data_t grab_interface = {
        .name   = "vswitch",
        .cancel = [=] () { algorithm->stop_switch(false); },
    };

  public:
    void init()
    {
        output->connect(&on_set_workspace_request);
        output->connect(&on_grabbed_view_disappear);

        algorithm = std::make_unique<vswitch_basic_plugin>(output,
            [=] () { output->deactivate_plugin(&grab_interface); });

        bindings = std::make_unique<wf::vswitch::control_bindings_t>(output);
        bindings->setup([this] (wf::point_t delta, wayfire_toplevel_view view, bool only_view)
        {
            // Do not switch workspace with sticky view, they are on all
            // workspaces anyway
            if (view && view->sticky)
            {
                view = nullptr;
            }

            if (this->set_capabilities(wf::CAPABILITY_MANAGE_DESKTOP))
            {
                if (delta == wf::point_t{0, 0})
                {
                    // Consume input event
                    return true;
                }

                if (only_view && view)
                {
                    auto size = output->get_screen_size();

                    for (auto& v : view->enumerate_views(false))
                    {
                        auto origin = wf::origin(v->get_pending_geometry());
                        v->move(origin.x + delta.x * size.width, origin.y + delta.y * size.height);
                    }

                    wf::view_change_workspace_signal data;
                    data.view = view;
                    data.from = output->wset()->get_current_workspace();
                    data.to   = data.from + delta;
                    output->emit(&data);
                    wf::get_core().seat->refocus();

                    return true;
                }

                return add_direction(delta, view);
            } else
            {
                return false;
            }
        });
    }

    inline bool is_active()
    {
        return output->is_plugin_active(grab_interface.name);
    }

    inline bool can_activate()
    {
        return is_active() || output->can_activate_plugin(&grab_interface);
    }

    /**
     * Check if we can switch the plugin capabilities.
     * This makes sense only if the plugin is already active, otherwise,
     * the operation can succeed.
     *
     * @param caps The additional capabilities required, aside from the
     *   base caps.
     */
    bool set_capabilities(uint32_t caps)
    {
        uint32_t total_caps = caps | base_caps;
        if (!is_active())
        {
            this->grab_interface.capabilities = total_caps;

            return true;
        }

        // already have everything needed
        if ((grab_interface.capabilities & total_caps) == total_caps)
        {
            // note: do not downgrade, in case total_caps are a subset of
            // current_caps
            return true;
        }

        // check for only the additional caps
        if (output->can_activate_plugin(caps))
        {
            grab_interface.capabilities = total_caps;

            return true;
        } else
        {
            return false;
        }
    }

    bool add_direction(wf::point_t delta, wayfire_view view = nullptr)
    {
        if (!is_active() && !start_switch())
        {
            return false;
        }

        if (view && ((view->role != wf::VIEW_ROLE_TOPLEVEL) || !view->is_mapped()))
        {
            view = nullptr;
        }

        algorithm->set_overlay_view(toplevel_cast(view));
        algorithm->set_target_workspace(
            output->wset()->get_current_workspace() + delta);

        return true;
    }

    wf::signal::connection_t<wf::view_disappeared_signal> on_grabbed_view_disappear =
        [=] (wf::view_disappeared_signal *ev)
    {
        if (ev->view == algorithm->get_overlay_view())
        {
            algorithm->set_overlay_view(nullptr);
        }
    };

    wf::signal::connection_t<wf::workspace_change_request_signal> on_set_workspace_request =
        [=] (wf::workspace_change_request_signal *ev)
    {
        if (ev->old_viewport == ev->new_viewport)
        {
            // nothing to do
            ev->carried_out = true;

            return;
        }

        if (is_active())
        {
            ev->carried_out = add_direction(ev->new_viewport - ev->old_viewport);
        } else
        {
            if (this->set_capabilities(0))
            {
                if (ev->fixed_views.size() > 2)
                {
                    LOGE("NOT IMPLEMENTED: ",
                        "changing workspace with more than 1 fixed view");
                }

                ev->carried_out = add_direction(ev->new_viewport - ev->old_viewport,
                    ev->fixed_views.empty() ? nullptr : ev->fixed_views[0]);
            }
        }
    };

    bool start_switch()
    {
        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        algorithm->start_switch();

        return true;
    }

    void fini()
    {
        if (is_active())
        {
            algorithm->stop_switch(false);
        }

        bindings->tear_down();
    }
};

class wf_vswitch_global_plugin_t : public wf::per_output_plugin_t<vswitch>
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

  public:
    void init() override
    {
        per_output_plugin_t::init();
        ipc_repo->register_method("vswitch/set-workspace", request_workspace);
    }

    void fini() override
    {
        per_output_plugin_t::fini();
        ipc_repo->unregister_method("vswitch/set-workspace");
    }

    wf::ipc::method_callback request_workspace = [=] (const wf::json_t& data)
    {
        uint64_t x = wf::ipc::json_get_uint64(data, "x");
        uint64_t y = wf::ipc::json_get_uint64(data, "y");
        uint64_t output_id = wf::ipc::json_get_uint64(data, "output-id");
        std::optional<uint64_t> view_id = wf::ipc::json_get_optional_uint64(data, "view-id");

        auto wo = wf::ipc::find_output_by_id(output_id);
        if (!wo)
        {
            return wf::ipc::json_error("Invalid output!");
        }

        auto grid_size = wo->wset()->get_workspace_grid_size();
        if ((int(data["x"]) >= grid_size.width) || (int(data["y"]) >= grid_size.height))
        {
            return wf::ipc::json_error("Workspace coordinates are too big!");
        }

        wayfire_toplevel_view switch_with_view;
        if (view_id.has_value())
        {
            auto view = toplevel_cast(wf::ipc::find_view_by_id(view_id.value()));
            if (!view)
            {
                return wf::ipc::json_error("Invalid view or view not toplevel!");
            }

            if (!view->is_mapped())
            {
                return wf::ipc::json_error("Cannot grab unmapped view!");
            }

            if (view->get_output() != wo)
            {
                return wf::ipc::json_error("Cannot grab view on a different output!");
            }

            switch_with_view = view;
        }

        if (output_instance[wo]->set_capabilities(wf::CAPABILITY_MANAGE_COMPOSITOR))
        {
            wf::point_t new_viewport = {int(x), int(y)};
            wf::point_t cur_viewport = wo->wset()->get_current_workspace();
            wf::point_t delta = new_viewport - cur_viewport;
            output_instance[wo]->add_direction(delta, switch_with_view);
        }

        return wf::ipc::json_ok();
    };
};

DECLARE_WAYFIRE_PLUGIN(wf_vswitch_global_plugin_t);
