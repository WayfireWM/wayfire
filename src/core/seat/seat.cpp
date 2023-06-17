#include "seat-impl.hpp"
#include "cursor.hpp"
#include "wayfire/compositor-view.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "../core-impl.hpp"
#include "../view/view-impl.hpp"
#include "keyboard.hpp"
#include "pointer.hpp"
#include "touch.hpp"
#include "input-manager.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/output-layout.hpp"
#include <wayfire/util/log.hpp>
#include "wayfire/scene-input.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/seat.hpp>
#include <string>
#include "../../view/view-keyboard-interaction.hpp"
#include "../../view/wlr-surface-pointer-interaction.hpp"


#include "drag-icon.hpp"
#include "wayfire/util.hpp"

wf::seat_t::~seat_t() = default;
void wf::seat_t::set_active_node(wf::scene::node_ptr node)
{
    priv->set_keyboard_focus(node);
}

uint32_t wf::seat_t::get_keyboard_modifiers()
{
    return priv->get_modifiers();
}

uint32_t wf::seat_t::modifier_from_keycode(uint32_t keycode)
{
    if (priv->current_keyboard)
    {
        return priv->current_keyboard->mod_from_key(keycode);
    }

    return 0;
}

xkb_state*wf::seat_t::get_xkb_state()
{
    if (priv->current_keyboard)
    {
        return priv->current_keyboard->handle->xkb_state;
    }

    return nullptr;
}

std::vector<uint32_t> wf::seat_t::get_pressed_keys()
{
    std::vector<uint32_t> pressed_keys{priv->pressed_keys.begin(), priv->pressed_keys.end()};
    return pressed_keys;
}

/* ----------------------- wf::seat_t implementation ------------------------ */
wf::seat_t::seat_t(wl_display *display, std::string name) : seat(wlr_seat_create(display, name.c_str()))
{
    priv = std::make_unique<impl>();
    priv->seat     = seat;
    priv->cursor   = std::make_unique<wf::cursor_t>(this);
    priv->lpointer = std::make_unique<wf::pointer_t>(
        wf::get_core_impl().input, nonstd::make_observer(this));
    priv->touch = std::make_unique<wf::touch_interface_t>(priv->cursor->cursor, seat,
        [] (const wf::pointf_t& global) -> wf::scene::node_ptr
    {
        auto value = wf::get_core().scene()->find_node_at(global);
        return value ? value->node->shared_from_this() : nullptr;
    });

    priv->request_start_drag.set_callback([&] (void *data)
    {
        auto ev = static_cast<wlr_seat_request_start_drag_event*>(data);
        priv->validate_drag_request(ev);
    });
    priv->request_start_drag.connect(&seat->events.request_start_drag);

    priv->start_drag.set_callback([&] (void *data)
    {
        auto d = static_cast<wlr_drag*>(data);
        if (d->icon)
        {
            this->priv->drag_icon = std::make_unique<wf::drag_icon_t>(d->icon);
        }

        this->priv->drag_active = true;
        priv->end_drag.set_callback([&] (void*)
        {
            this->priv->drag_active = false;
            priv->end_drag.disconnect();
        });
        priv->end_drag.connect(&d->events.destroy);
    });
    priv->start_drag.connect(&seat->events.start_drag);

    priv->request_set_selection.set_callback([&] (void *data)
    {
        auto ev = static_cast<wlr_seat_request_set_selection_event*>(data);
        wlr_seat_set_selection(wf::get_core().get_current_seat(), ev->source, ev->serial);
    });
    priv->request_set_selection.connect(&seat->events.request_set_selection);

    priv->request_set_primary_selection.set_callback([&] (void *data)
    {
        auto ev = static_cast<wlr_seat_request_set_primary_selection_event*>(data);
        wlr_seat_set_primary_selection(wf::get_core().get_current_seat(), ev->source, ev->serial);
    });
    priv->request_set_primary_selection.connect(&seat->events.request_set_primary_selection);

    priv->on_wlr_keyboard_grab_end.set_callback([&] (void*)
    {
        if (priv->keyboard_focus &&
            dynamic_cast<view_keyboard_interaction_t*>(&priv->keyboard_focus->keyboard_interaction()))
        {
            priv->keyboard_focus->keyboard_interaction().handle_keyboard_enter(this);
        }
    });
    priv->on_wlr_keyboard_grab_end.connect(&seat->events.keyboard_grab_end);

    priv->on_wlr_pointer_grab_end.set_callback([&] (void*)
    {
        if (priv->drag_active)
        {
            // Drag is handled separately.
            return;
        }

        if (auto focus = priv->lpointer->get_focus())
        {
            if (dynamic_cast<wlr_surface_pointer_interaction_t*>(&focus->pointer_interaction()))
            {
                wf::pointf_t local = get_node_local_coords(focus.get(), priv->cursor->get_cursor_position());
                focus->pointer_interaction().handle_pointer_enter(local);
            }
        }
    });
    priv->on_wlr_pointer_grab_end.connect(&seat->events.pointer_grab_end);

    priv->on_new_device = [&] (wf::input_device_added_signal *ev)
    {
        switch (ev->device->get_wlr_handle()->type)
        {
          case WLR_INPUT_DEVICE_KEYBOARD:
            this->priv->keyboards.emplace_back(std::make_unique<wf::keyboard_t>(
                ev->device->get_wlr_handle()));
            if (this->priv->current_keyboard == nullptr)
            {
                priv->set_keyboard(priv->keyboards.back().get());
            }

            break;

          case WLR_INPUT_DEVICE_TOUCH:
          case WLR_INPUT_DEVICE_POINTER:
          case WLR_INPUT_DEVICE_TABLET_TOOL:
            this->priv->cursor->add_new_device(ev->device->get_wlr_handle());
            break;

          default:
            break;
        }

        priv->update_capabilities();
    };

    priv->on_remove_device = [&] (wf::input_device_removed_signal *ev)
    {
        auto dev = ev->device->get_wlr_handle();
        if (dev->type == WLR_INPUT_DEVICE_KEYBOARD)
        {
            bool current_kbd_destroyed = false;
            if (priv->current_keyboard && (priv->current_keyboard->device == dev))
            {
                current_kbd_destroyed = true;
            }

            auto it = std::remove_if(priv->keyboards.begin(), priv->keyboards.end(),
                [=] (const std::unique_ptr<wf::keyboard_t>& kbd)
            {
                return kbd->device == dev;
            });

            priv->keyboards.erase(it, priv->keyboards.end());

            if (current_kbd_destroyed && priv->keyboards.size())
            {
                priv->set_keyboard(priv->keyboards.front().get());
            } else
            {
                priv->set_keyboard(nullptr);
            }
        }

        priv->update_capabilities();
    };

    wf::get_core().connect(&priv->on_new_device);
    wf::get_core().connect(&priv->on_remove_device);
}

void wf::seat_t::impl::update_capabilities()
{
    uint32_t caps = 0;
    for (const auto& dev : wf::get_core().get_input_devices())
    {
        switch (dev->get_wlr_handle()->type)
        {
          case WLR_INPUT_DEVICE_KEYBOARD:
            caps |= WL_SEAT_CAPABILITY_KEYBOARD;
            break;

          case WLR_INPUT_DEVICE_POINTER:
            caps |= WL_SEAT_CAPABILITY_POINTER;
            break;

          case WLR_INPUT_DEVICE_TOUCH:
            caps |= WL_SEAT_CAPABILITY_TOUCH;
            break;

          default:
            break;
        }
    }

    wlr_seat_set_capabilities(seat, caps);
}

void wf::seat_t::impl::validate_drag_request(wlr_seat_request_start_drag_event *ev)
{
    auto seat = wf::get_core().get_current_seat();

    if (wlr_seat_validate_pointer_grab_serial(seat, ev->origin, ev->serial))
    {
        wlr_seat_start_pointer_drag(seat, ev->drag, ev->serial);
        return;
    }

    struct wlr_touch_point *point;
    if (wlr_seat_validate_touch_grab_serial(seat, ev->origin, ev->serial, &point))
    {
        wlr_seat_start_touch_drag(seat, ev->drag, ev->serial, point);
        return;
    }

    LOGD("Ignoring start_drag request: ",
        "could not validate pointer or touch serial ", ev->serial);
    wlr_data_source_destroy(ev->drag->source);
}

void wf::seat_t::impl::update_drag_icon()
{
    if (drag_icon)
    {
        drag_icon->update_position();
    }
}

void wf::seat_t::impl::set_keyboard(wf::keyboard_t *keyboard)
{
    this->current_keyboard = keyboard;
    wlr_seat_set_keyboard(seat, keyboard ? wlr_keyboard_from_input_device(keyboard->device) : NULL);
}

void wf::seat_t::impl::break_mod_bindings()
{
    for (auto& kbd : this->keyboards)
    {
        kbd->mod_binding_key = 0;
    }
}

uint32_t wf::seat_t::impl::get_modifiers()
{
    return current_keyboard ? current_keyboard->get_modifiers() : 0;
}

void wf::seat_t::impl::force_release_keys()
{
    if (this->keyboard_focus)
    {
        // Release currently pressed buttons
        for (auto key : this->pressed_keys)
        {
            wlr_keyboard_key_event ev;
            ev.keycode = key;
            ev.state   = WL_KEYBOARD_KEY_STATE_RELEASED;
            ev.update_state = true;
            ev.time_msec    = get_current_time();
            this->keyboard_focus->keyboard_interaction().handle_keyboard_key(wf::get_core().seat.get(), ev);
        }
    }
}

void wf::seat_t::impl::transfer_grab(wf::scene::node_ptr grab_node)
{
    if (this->keyboard_focus == grab_node)
    {
        return;
    }

    if (this->keyboard_focus)
    {
        this->keyboard_focus->keyboard_interaction().handle_keyboard_leave(wf::get_core().seat.get());
    }

    this->keyboard_focus = grab_node;
    grab_node->keyboard_interaction().handle_keyboard_enter(wf::get_core().seat.get());

    wf::keyboard_focus_changed_signal data;
    data.new_focus = grab_node;
    wf::get_core().emit(&data);
}

void wf::seat_t::impl::set_keyboard_focus(wf::scene::node_ptr new_focus)
{
    if (this->keyboard_focus == new_focus)
    {
        return;
    }

    if (this->keyboard_focus)
    {
        this->keyboard_focus->keyboard_interaction().handle_keyboard_leave(wf::get_core().seat.get());
    }

    this->keyboard_focus = new_focus;
    if (new_focus)
    {
        new_focus->keyboard_interaction().handle_keyboard_enter(wf::get_core().seat.get());
    }

    wf::keyboard_focus_changed_signal data;
    data.new_focus = new_focus;
    wf::get_core().emit(&data);
}

namespace wf
{
wlr_input_device*input_device_t::get_wlr_handle()
{
    return handle;
}

bool input_device_t::set_enabled(bool enabled)
{
    if (enabled == is_enabled())
    {
        return true;
    }

    if (!wlr_input_device_is_libinput(handle))
    {
        return false;
    }

    auto dev = wlr_libinput_get_device_handle(handle);
    assert(dev);

    libinput_device_config_send_events_set_mode(dev,
        enabled ? LIBINPUT_CONFIG_SEND_EVENTS_ENABLED :
        LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);

    return true;
}

bool input_device_t::is_enabled()
{
    /* Currently no support for enabling/disabling non-libinput devices */
    if (!wlr_input_device_is_libinput(handle))
    {
        return true;
    }

    auto dev = wlr_libinput_get_device_handle(handle);
    assert(dev);

    auto mode = libinput_device_config_send_events_get_mode(dev);

    return mode == LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
}

input_device_t::input_device_t(wlr_input_device *handle)
{
    this->handle = handle;
}
} // namespace wf

wf::input_device_impl_t::input_device_impl_t(wlr_input_device *dev) :
    wf::input_device_t(dev)
{
    on_destroy.set_callback([&] (void*)
    {
        wf::get_core_impl().input->handle_input_destroyed(this->get_wlr_handle());
    });
    on_destroy.connect(&dev->events.destroy);
}

static wf::pointf_t to_local_recursive(wf::scene::node_t *node, wf::pointf_t point)
{
    if (node->parent())
    {
        return node->to_local(to_local_recursive(node->parent(), point));
    }

    return node->to_local(point);
}

wf::pointf_t get_node_local_coords(wf::scene::node_t *node,
    const wf::pointf_t& point)
{
    return to_local_recursive(node, point);
}

bool is_grabbed_node_alive(wf::scene::node_ptr node)
{
    auto cur = node.get();
    while (cur)
    {
        if (!cur->is_enabled())
        {
            return false;
        }

        if (cur == wf::get_core().scene().get())
        {
            return true;
        }

        cur = cur->parent();
    }

    // We did not reach the scenegraph root => we cannot focus the node anymore, it was removed.
    return false;
}
