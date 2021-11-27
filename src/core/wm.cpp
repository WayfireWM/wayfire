#include "wm.hpp"
#include "wayfire/output.hpp"
#include "wayfire/view.hpp"
#include "wayfire/core.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/output-layout.hpp"

#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include "../output/output-impl.hpp"
#include "wayfire/signal-definitions.hpp"

#include <linux/input-event-codes.h>

static void idle_shutdown(void *data)
{
    wf::get_core().shutdown();
}

void wayfire_exit::init()
{
    key = [] (const wf::keybinding_t&)
    {
        auto output_impl =
            static_cast<wf::output_impl_t*>(wf::get_core().get_active_output());
        if (output_impl->is_inhibited())
        {
            return false;
        }

        idle_shutdown(nullptr);

        return true;
    };

    // make sure to shut down wayfire if destroying the last
    // nested backend output
    on_output_removed.set_callback([=] (wf::signal_data_t *data)
    {
        auto output = wf::get_signaled_output(data);

        bool is_nested_compositor = wlr_output_is_wl(output->handle);
#if WLR_HAS_X11_BACKEND
        is_nested_compositor |= wlr_output_is_x11(output->handle);
#endif

        int cnt_other_outputs = 0;
        for (auto& wo : wf::get_core().output_layout->get_outputs())
        {
            if ((wo != output) && !wlr_output_is_noop(wo->handle))
            {
                ++cnt_other_outputs;
            }
        }

        if (is_nested_compositor && (cnt_other_outputs == 0))
        {
            wl_event_loop_add_idle(wf::get_core().ev_loop, idle_shutdown, nullptr);
        }
    });

    output->connect_signal("pre-remove", &on_output_removed);
    output->add_key(wf::create_option_string<wf::keybinding_t>(
        "<ctrl> <alt> KEY_BACKSPACE"), &key);
}

void wayfire_exit::fini()
{
    output->rem_binding(&key);
}

void wayfire_close::init()
{
    grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;
    wf::option_wrapper_t<wf::activatorbinding_t> key("core/close_top_view");
    callback = [=] (const wf::activator_data_t& ev)
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        output->deactivate_plugin(grab_interface);
        auto view = output->get_active_view();
        if (view && (view->role == wf::VIEW_ROLE_TOPLEVEL))
        {
            view->close();
        }

        return true;
    };

    output->add_activator(key, &callback);
}

void wayfire_close::fini()
{
    output->rem_binding(&callback);
}

void wayfire_focus::init()
{
    grab_interface->name = "_wf_focus";
    grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP;

    on_wm_focus_request.set_callback([=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<wm_focus_request*>(data);
        check_focus_surface(ev->focus);
    });
    output->connect_signal("wm-focus-request", &on_wm_focus_request);

    on_button.set_callback([=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_event_pointer_button>*>(data);

        if (ev->event->state != WLR_BUTTON_PRESSED)
        {
            return;
        }

        /* focuse_btns->get_value() does not compile */
        wf::option_sptr_t<wf::activatorbinding_t> tmp = focus_btns;
        if ((!focus_modifiers && wf::get_core().get_keyboard_modifiers()) ||
            !tmp->get_value().has_match(wf::buttonbinding_t(0, ev->event->button)))
        {
            return;
        }

        bool changed_focus =
            this->check_focus_surface(wf::get_core().get_cursor_focus());

        bool pass_through = (pass_btns || !changed_focus);
        ev->mode = pass_through ? wf::input_event_processing_mode_t::FULL :
            wf::input_event_processing_mode_t::NO_CLIENT;
    });
    wf::get_core().connect_signal("pointer_button", &on_button);

    // build touch gesture
    auto on_tap = std::make_unique<wf::touch::touch_action_t>(1, true);
    std::vector<std::unique_ptr<wf::touch::gesture_action_t>> actions;
    actions.emplace_back(std::move(on_tap));
    const auto& on_tap_action = [this] ()
    {
        if (wf::get_core().get_active_output() == this->output)
        {
            this->check_focus_surface(wf::get_core().get_touch_focus());
        }
    };

    this->tap_gesture =
        std::make_unique<wf::touch::gesture_t>(std::move(actions), on_tap_action);
    wf::get_core().add_touch_gesture(tap_gesture);
}

bool wayfire_focus::check_focus_surface(wf::focused_view_t focus)
{
    /* Find the main view */
    if (!focus || !focus.view()->is_mapped() ||
        !output->can_activate_plugin(grab_interface->capabilities))
    {
        return false;
    }

    auto target_wo = focus.view()->get_output();
    auto old_focus = target_wo->get_active_view();
    if (focus.view()->get_keyboard_focus().accepts_focus())
    {
        target_wo->focus_view(focus.view(), true);
    } else
    {
        target_wo->workspace->bring_to_front(focus.view());
    }

    return target_wo->get_active_view() != old_focus;
}

void wayfire_focus::fini()
{
    output->rem_binding(&on_button);
    wf::get_core().rem_touch_gesture(tap_gesture);
}
