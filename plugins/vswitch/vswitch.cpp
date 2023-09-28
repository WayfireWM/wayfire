#include "wayfire/core.hpp"
#include <wayfire/plugins/vswitch.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <linux/input.h>
#include <wayfire/util/log.hpp>
#include <wayfire/seat.hpp>

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

        if (view && (view->role != wf::VIEW_ROLE_TOPLEVEL))
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

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<vswitch>);
