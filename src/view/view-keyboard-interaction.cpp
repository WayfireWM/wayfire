#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include <wayfire/view.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/core.hpp>

#include <wayfire/nonstd/wlroots-full.hpp>
/**
 * An interface for scene nodes which interact with the keyboard.
 */
class view_keyboard_interaction_t : public wf::keyboard_interaction_t
{
    wayfire_view view;

  public:
    view_keyboard_interaction_t(wayfire_view _view)
    {
        this->view = _view;
    }

    void handle_keyboard_enter() override
    {
        if (view->get_wlr_surface())
        {
            auto seat = wf::get_core().get_current_seat();
            auto kbd  = wlr_seat_get_keyboard(seat);
            wlr_seat_keyboard_notify_enter(seat,
                view->get_wlr_surface(),
                kbd ? kbd->keycodes : NULL,
                kbd ? kbd->num_keycodes : 0,
                kbd ? &kbd->modifiers : NULL);
        }
    }

    void handle_keyboard_leave() override
    {
        if (view->get_wlr_surface())
        {
            auto seat = wf::get_core().get_current_seat();
            wlr_seat_keyboard_notify_clear_focus(seat);
        }
    }

    void handle_keyboard_key(wlr_keyboard_key_event event) override
    {
        auto seat = wf::get_core().get_current_seat();
        wlr_seat_keyboard_notify_key(seat,
            event.time_msec, event.keycode, event.state);
    }
};
