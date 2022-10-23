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

    // The 'active' state is used to denote the last toplevel view which was
    // focused. When focusing a view, we need to make sure that this state
    // is correctly updated.
    static wayfire_view last_focused_toplevel;

    wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped =
        [] (wf::view_unmapped_signal *signal)
    {
        if (signal->view == last_focused_toplevel)
        {
            last_focused_toplevel = nullptr;
        }
    };

  public:
    view_keyboard_interaction_t(wayfire_view _view)
    {
        this->view = _view;
        view->connect(&view_unmapped);
    }

    void handle_keyboard_enter() override
    {
        if (this->view->role == wf::VIEW_ROLE_TOPLEVEL)
        {
            if (last_focused_toplevel != this->view)
            {
                if (last_focused_toplevel)
                {
                    last_focused_toplevel->set_activated(false);
                }

                this->view->set_activated(true);
                last_focused_toplevel = this->view;
            }
        }

        auto iv = interactive_view_from_view(view.get());
        if (iv)
        {
            iv->handle_keyboard_enter();
        } else if (view->get_wlr_surface())
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
        auto oiv = interactive_view_from_view(view.get());
        if (oiv)
        {
            oiv->handle_keyboard_leave();
        } else if (view->get_wlr_surface())
        {
            auto seat = wf::get_core().get_current_seat();
            wlr_seat_keyboard_notify_clear_focus(seat);
        }
    }

    void handle_keyboard_key(wlr_keyboard_key_event event) override
    {
        auto iv = interactive_view_from_view(view.get());
        if (iv)
        {
            iv->handle_key(event.keycode, event.state);
        }

        auto seat = wf::get_core().get_current_seat();
        wlr_seat_keyboard_notify_key(seat,
            event.time_msec, event.keycode, event.state);
    }
};

wayfire_view view_keyboard_interaction_t::last_focused_toplevel;
