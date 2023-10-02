#include "wayfire/core.hpp"
#include "wayfire/plugins/common/input-grab.hpp"
#include "wayfire/plugins/common/util.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/view-helpers.hpp"
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>

/*
 * This plugin provides abilities to switch between views.
 * It works similarly to the alt-esc binding in Windows or GNOME
 */

class wayfire_fast_switcher : public wf::per_output_plugin_instance_t, public wf::keyboard_interaction_t
{
    wf::option_wrapper_t<wf::keybinding_t> activate_key{"fast-switcher/activate"};
    wf::option_wrapper_t<wf::keybinding_t> activate_key_backward{
        "fast-switcher/activate_backward"};
    wf::option_wrapper_t<double> inactive_alpha{"fast-switcher/inactive_alpha"};
    std::vector<wayfire_toplevel_view> views; // all views on current viewport
    size_t current_view_index = 0;
    // the modifiers which were used to activate switcher
    uint32_t activating_modifiers = 0;
    bool active = false;
    std::unique_ptr<wf::input_grab_t> input_grab;

    wf::plugin_activation_data_t grab_interface = {
        .name = "fast-switcher",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
    };

  public:
    void init() override
    {
        output->add_key(activate_key, &fast_switch);
        output->add_key(activate_key_backward, &fast_switch_backward);
        input_grab = std::make_unique<wf::input_grab_t>("fast-switch", output, this, nullptr, nullptr);
        grab_interface.cancel = [=] () { switch_terminate(); };
    }

    void handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event event) override
    {
        auto mod = wf::get_core().seat->modifier_from_keycode(event.keycode);
        if ((event.state == WLR_KEY_RELEASED) && (mod & activating_modifiers))
        {
            switch_terminate();
        }
    }

    void view_chosen(int i, bool reorder_only)
    {
        /* No view available */
        if (!((0 <= i) && (i < (int)views.size())))
        {
            return;
        }

        current_view_index = i;
        set_view_highlighted(views[i], true);

        for (int i = (int)views.size() - 1; i >= 0; i--)
        {
            wf::view_bring_to_front(views[i]);
        }

        if (reorder_only)
        {
            wf::view_bring_to_front(views[i]);
        } else
        {
            wf::get_core().default_wm->focus_raise_view(views[i]);
        }
    }

    wf::signal::connection_t<wf::view_disappeared_signal> cleanup_view = [=] (wf::view_disappeared_signal *ev)
    {
        size_t i = 0;
        for (; i < views.size() && views[i] != ev->view; i++)
        {}

        if (i == views.size())
        {
            return;
        }

        views.erase(views.begin() + i);

        if (views.empty())
        {
            switch_terminate();

            return;
        }

        if (i <= current_view_index)
        {
            int new_index =
                (current_view_index + views.size() - 1) % views.size();
            view_chosen(new_index, true);
        }
    };

    const std::string transformer_name = "fast-switcher";

    void set_view_alpha(wayfire_view view, float alpha)
    {
        auto tr = wf::ensure_named_transformer<wf::scene::view_2d_transformer_t>(
            view, wf::TRANSFORMER_2D, transformer_name, view);
        view->get_transformed_node()->begin_transform_update();
        tr->alpha = alpha;
        view->get_transformed_node()->end_transform_update();
    }

    void set_view_highlighted(wayfire_toplevel_view view, bool selected)
    {
        // set alpha and decoration to indicate selected view
        view->set_activated(selected); // changes decoration focus state
        double alpha = selected ? 1.0 : inactive_alpha;
        set_view_alpha(view, alpha);
    }

    void update_views()
    {
        views = output->wset()->get_views(
            wf::WSET_CURRENT_WORKSPACE | wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED);
        std::sort(views.begin(), views.end(), [] (wayfire_toplevel_view& a, wayfire_toplevel_view& b)
        {
            return wf::get_focus_timestamp(a) > wf::get_focus_timestamp(b);
        });
    }

    bool do_switch(bool forward)
    {
        if (active)
        {
            switch_next(forward);
            return true;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        update_views();

        if (views.size() < 1)
        {
            output->deactivate_plugin(&grab_interface);
            return false;
        }

        current_view_index = 0;
        active = true;

        /* Set all to semi-transparent */
        for (auto view : views)
        {
            set_view_highlighted(view, false);
        }

        input_grab->grab_input(wf::scene::layer::OVERLAY);
        activating_modifiers = wf::get_core().seat->get_keyboard_modifiers();
        switch_next(forward);

        output->connect(&cleanup_view);
        return true;
    }

    wf::key_callback fast_switch = [=] (auto)
    {
        return do_switch(true);
    };

    wf::key_callback fast_switch_backward = [=] (auto)
    {
        return do_switch(false);
    };

    void switch_terminate()
    {
        // May modify alpha
        view_chosen(current_view_index, false);

        // Ungrab after selecting the correct view, so that it gets the focus directly
        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);

        // Remove transformers after modifying alpha
        for (auto view : views)
        {
            view->get_transformed_node()->rem_transformer(transformer_name);
        }

        active = false;
        cleanup_view.disconnect();
    }

    void switch_next(bool forward)
    {
        set_view_highlighted(views[current_view_index], false); // deselect last view

        int index = current_view_index;
        if (forward)
        {
            index = (index + 1) % views.size();
        } else
        {
            index = index ? index - 1 : views.size() - 1;
        }

        view_chosen(index, true);
    }

    void fini() override
    {
        if (active)
        {
            switch_terminate();
        }

        output->rem_binding(&fast_switch);
        output->rem_binding(&fast_switch_backward);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_fast_switcher>);
