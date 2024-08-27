#include "wayfire/core.hpp"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/util.hpp"
#include "wayfire/seat.hpp"
#include "wayfire/view.hpp"
#include "wayfire/matcher.hpp"
#include "wayfire/bindings-repository.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/plugin.hpp>
#include <wayland-server-protocol.h>
#include <map>

class wayfire_shortcuts_inhibit : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        inhibit_manager = wlr_keyboard_shortcuts_inhibit_v1_create(wf::get_core().display);

        keyboard_inhibit_new.set_callback([&] (void *data)
        {
            auto wlr_inhibitor = (struct wlr_keyboard_shortcuts_inhibitor_v1*)data;
            if (inhibitors.count(wlr_inhibitor->surface))
            {
                LOGE("Duplicate inhibitors for one surface not supported!");
                return;
            }

            inhibitors[wlr_inhibitor->surface] = std::make_unique<inhibitor_t>();
            auto& inhibitor = inhibitors[wlr_inhibitor->surface];

            inhibitor->inhibitor = wlr_inhibitor;
            inhibitor->on_destroy.set_callback([=] (auto)
            {
                deactivate_for_surface(wlr_inhibitor->surface);
                this->inhibitors.erase(wlr_inhibitor->surface);
            });
            inhibitor->on_destroy.connect(&wlr_inhibitor->events.destroy);
            check_inhibit(wf::get_core().seat->get_active_node());
        });
        keyboard_inhibit_new.connect(&inhibit_manager->events.new_inhibitor);
        wf::get_core().connect(&on_kb_focus_change);
        wf::get_core().connect(&on_view_mapped);
        wf::get_core().connect(&on_key_press);
    }

    void fini() override
    {}

    void check_inhibit(wf::scene::node_ptr focus)
    {
        auto focus_view = focus ? wf::node_to_view(focus) : nullptr;
        wlr_surface *new_focus = focus_view ? focus_view->get_keyboard_focus_surface() : nullptr;
        if (!inhibitors.count(new_focus))
        {
            new_focus = nullptr;
        }

        if (new_focus == last_focus)
        {
            return;
        }

        deactivate_for_surface(last_focus);

        if (ignore_views.matches(focus_view))
        {
            return;
        }

        activate_for_surface(new_focus);
    }

    bool is_unloadable() override
    {
        return false;
    }

  private:
    wf::view_matcher_t ignore_views{"shortcuts-inhibit/ignore_views"};
    wlr_keyboard_shortcuts_inhibit_manager_v1 *inhibit_manager;
    wf::wl_listener_wrapper keyboard_inhibit_new;
    wf::view_matcher_t inhibit_by_default{"shortcuts-inhibit/inhibit_by_default"};

    struct inhibitor_t
    {
        bool active = false;
        wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;
        wf::wl_listener_wrapper on_destroy;
    };

    std::map<wlr_surface*, std::unique_ptr<inhibitor_t>> inhibitors;
    wlr_surface *last_focus = nullptr;

    void activate_for_surface(wlr_surface *surface)
    {
        if (!surface)
        {
            return;
        }

        auto& inhibitor = inhibitors[surface];
        if (!inhibitor->active)
        {
            LOGD("Activating inhibitor for surface ", surface);
            wf::get_core().bindings->set_enabled(false);

            if (inhibitor->inhibitor)
            {
                wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor->inhibitor);
            }

            inhibitor->active = true;
        }

        last_focus = surface;
    }

    void deactivate_for_surface(wlr_surface *surface)
    {
        if (!surface)
        {
            return;
        }

        auto& inhibitor = inhibitors[surface];
        if (inhibitor->active)
        {
            LOGD("Deactivating inhibitor for surface ", surface);
            wf::get_core().bindings->set_enabled(true);

            if (inhibitor->inhibitor)
            {
                wlr_keyboard_shortcuts_inhibitor_v1_deactivate(inhibitor->inhibitor);
            }

            inhibitor->active = false;
        }

        last_focus = nullptr;
    }

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_kb_focus_change =
        [=] (wf::keyboard_focus_changed_signal *ev)
    {
        check_inhibit(ev->new_focus);
    };

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        if (inhibit_by_default.matches(ev->view) && ev->view->get_keyboard_focus_surface())
        {
            auto surface = ev->view->get_keyboard_focus_surface();
            inhibitors[surface] = std::make_unique<inhibitor_t>();
            auto& inhibitor = inhibitors[surface];

            inhibitor->inhibitor = nullptr;
            inhibitor->on_destroy.set_callback([this, surface] (auto)
            {
                deactivate_for_surface(surface);
                this->inhibitors.erase(surface);
            });
            inhibitor->on_destroy.connect(&surface->events.destroy);
            check_inhibit(wf::get_core().seat->get_active_node());
        }
    };

    wf::option_wrapper_t<wf::keybinding_t> break_grab_key{"shortcuts-inhibit/break_grab"};

    wf::signal::connection_t<wf::input_event_signal<wlr_keyboard_key_event>> on_key_press =
        [=] (wf::input_event_signal<wlr_keyboard_key_event> *ev)
    {
        auto break_key = break_grab_key.value();

        if ((ev->event->state == WL_KEYBOARD_KEY_STATE_PRESSED) &&
            (wf::get_core().seat->get_keyboard_modifiers() == break_key.get_modifiers()) &&
            (ev->event->keycode == break_key.get_key()))
        {
            LOGD("Force-break active inhibitor");
            deactivate_for_surface(last_focus);
        }
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_shortcuts_inhibit);
