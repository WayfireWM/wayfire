#pragma once

#include <wayfire/view.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>

#include <wayfire/nonstd/wlroots-full.hpp>

namespace wf
{
/**
 * An implementation of keyboard_interaction_t for wlr_surface-based views.
 */
class wlr_view_keyboard_interaction_t : public wf::keyboard_interaction_t
{
    std::weak_ptr<wf::view_interface_t> view;
    bool force_enter;

  public:
    wlr_view_keyboard_interaction_t(wayfire_view _view, bool force_enter = false)
    {
        this->view = _view->weak_from_this();
        this->force_enter = force_enter;
    }

    void handle_keyboard_enter(wf::seat_t *seat) override
    {
        if (auto ptr = view.lock())
        {
            if (ptr->get_wlr_surface())
            {
                auto pressed_keys = seat->get_pressed_keys();

                auto kbd = wlr_seat_get_keyboard(seat->seat);

                if (force_enter)
                {
                    wlr_seat_keyboard_enter(seat->seat, ptr->get_wlr_surface(),
                        pressed_keys.data(), pressed_keys.size(), kbd ? &kbd->modifiers : NULL);
                } else
                {
                    wlr_seat_keyboard_notify_enter(seat->seat, ptr->get_wlr_surface(),
                        pressed_keys.data(), pressed_keys.size(), kbd ? &kbd->modifiers : NULL);
                }
            }
        }
    }

    void handle_keyboard_leave(wf::seat_t *seat) override
    {
        if (auto ptr = view.lock())
        {
            wlr_seat_keyboard_notify_clear_focus(seat->seat);
        }
    }

    void handle_keyboard_key(wf::seat_t *seat, wlr_keyboard_key_event event) override
    {
        wlr_seat_keyboard_notify_key(seat->seat, event.time_msec, event.keycode, event.state);
    }
};
}
