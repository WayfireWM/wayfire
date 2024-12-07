#include "wm.hpp"
#include "wayfire/output.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/view.hpp"
#include "wayfire/core.hpp"
#include "wayfire/workspace-set.hpp"
#include "wayfire/output-layout.hpp"
#include <wayfire/seat.hpp>
#include <wayfire/window-manager.hpp>

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
    wf::option_wrapper_t<wf::keybinding_t> wayifre_exit_binding{"core/exit"};

    key = [] (const wf::keybinding_t&)
    {
        auto output_impl = static_cast<wf::output_impl_t*>(wf::get_core().seat->get_active_output());
        if (output_impl->is_inhibited())
        {
            return false;
        }

        idle_shutdown(nullptr);
        return true;
    };

    output->add_key(wayifre_exit_binding, &key);
}

void wayfire_exit::fini()
{
    output->rem_binding(&key);
}

void wayfire_close::init()
{
    wf::option_wrapper_t<wf::activatorbinding_t> key("core/close_top_view");
    callback = [=] (const wf::activator_data_t& ev)
    {
        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        output->deactivate_plugin(&grab_interface);
        auto view = wf::get_core().seat->get_active_view();
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
    on_pointer_button = [=] (wf::input_event_signal<wlr_pointer_button_event> *ev)
    {
        if ((ev->mode == wf::input_event_processing_mode_t::IGNORE) ||
            (ev->event->state != WL_POINTER_BUTTON_STATE_PRESSED))
        {
            return;
        }

        /* focus_btns->get_value() does not compile */
        wf::option_sptr_t<wf::activatorbinding_t> tmp = focus_btns;
        if ((!focus_modifiers && wf::get_core().seat->get_keyboard_modifiers()) ||
            !tmp->get_value().has_match(wf::buttonbinding_t(0, ev->event->button)))
        {
            return;
        }

        bool changed_focus =
            this->check_focus_surface(wf::get_core().get_cursor_focus_view());

        bool pass_through = (pass_btns || !changed_focus);
        ev->mode = pass_through ? wf::input_event_processing_mode_t::FULL :
            wf::input_event_processing_mode_t::NO_CLIENT;
    };
    wf::get_core().connect(&on_pointer_button);

    // build touch gesture
    auto on_tap = std::make_unique<wf::touch::touch_action_t>(1, true);
    std::vector<std::unique_ptr<wf::touch::gesture_action_t>> actions;
    actions.emplace_back(std::move(on_tap));
    const auto& on_tap_action = [this] ()
    {
        this->check_focus_surface(wf::get_core().get_touch_focus_view());
    };

    this->tap_gesture =
        std::make_unique<wf::touch::gesture_t>(std::move(actions), on_tap_action);
    wf::get_core().add_touch_gesture(tap_gesture);
}

bool wayfire_focus::check_focus_surface(wayfire_view view)
{
    auto& core = wf::get_core();
    if (!view || !view->is_mapped() || !core.seat->get_active_output() ||
        !core.seat->get_active_output()->can_activate_plugin(grab_interface.capabilities))
    {
        return false;
    }

    if (!view->get_keyboard_focus_surface())
    {
        wf::view_bring_to_front(view);
        return false;
    }

    auto old_focus = core.seat->get_active_view();
    core.default_wm->focus_raise_view(view);
    return core.seat->get_active_view() != old_focus;
}

void wayfire_focus::fini()
{
    wf::get_core().rem_touch_gesture(tap_gesture);
}
