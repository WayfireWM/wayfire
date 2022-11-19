#include "seat.hpp"
#include "cursor.hpp"
#include "wayfire/compositor-view.hpp"
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
#include "wayfire/signal-definitions.hpp"
#include <wayfire/nonstd/wlroots.hpp>

/* ------------------------ Drag icon impl ---------------------------------- */
wf::drag_icon_t::drag_icon_t(wlr_drag_icon *ic) :
    wf::wlr_child_surface_base_t(this), icon(ic)
{
    on_map.set_callback([&] (void*) { this->map(icon->surface); });
    on_unmap.set_callback([&] (void*) { this->unmap(); });
    on_destroy.set_callback([&] (void*)
    {
        /* we don't dec_keep_count() because the surface memory is
         * managed by the unique_ptr */
        wf::get_core_impl().seat->drag_icon = nullptr;
    });

    on_map.connect(&icon->events.map);
    on_unmap.connect(&icon->events.unmap);
    on_destroy.connect(&icon->events.destroy);
}

wf::point_t wf::drag_icon_t::get_offset()
{
    auto pos = icon->drag->grab_type == WLR_DRAG_GRAB_KEYBOARD_TOUCH ?
        wf::get_core().get_touch_position(icon->drag->touch_id) :
        wf::get_core().get_cursor_position();

    if (is_mapped())
    {
        pos.x += icon->surface->sx;
        pos.y += icon->surface->sy;
    }

    return {(int)pos.x, (int)pos.y};
}

void wf::drag_icon_t::damage()
{
    // damage previous position
    damage_surface_box_global(last_box);

    // damage new position
    last_box = {0, 0, get_size().width, get_size().height};
    last_box = last_box + get_offset();
    damage_surface_box_global(last_box);
}

void wf::drag_icon_t::damage_surface_box(const wlr_box& box)
{
    if (!is_mapped())
    {
        return;
    }

    damage_surface_box_global(box + this->get_offset());
}

void wf::drag_icon_t::damage_surface_box_global(const wlr_box& rect)
{
    for (auto& output : wf::get_core().output_layout->get_outputs())
    {
        auto output_geometry = output->get_layout_geometry();
        if (output_geometry & rect)
        {
            auto local = rect;
            local.x -= output_geometry.x;
            local.y -= output_geometry.y;
            output->render->damage(local);
        }
    }
}

/* ----------------------- wf::seat_t implementation ------------------------ */
wf::seat_t::seat_t()
{
    seat     = wlr_seat_create(wf::get_core().display, "default");
    cursor   = std::make_unique<wf::cursor_t>(this);
    lpointer = std::make_unique<wf::pointer_t>(
        wf::get_core_impl().input, nonstd::make_observer(this));
    touch = std::make_unique<wf::touch_interface_t>(cursor->cursor, seat,
        [] (wf::pointf_t global, wf::pointf_t& local)
    {
        return wf::get_core_impl().input->input_surface_at(global, local);
    });

    request_start_drag.set_callback([&] (void *data)
    {
        auto ev = static_cast<wlr_seat_request_start_drag_event*>(data);
        validate_drag_request(ev);
    });
    request_start_drag.connect(&seat->events.request_start_drag);

    start_drag.set_callback([&] (void *data)
    {
        auto d = static_cast<wlr_drag*>(data);
        if (d->icon)
        {
            this->drag_icon = std::make_unique<wf::drag_icon_t>(d->icon);

            // Sometimes, the drag surface is reused between two or more drags.
            // In this case, when the drag starts, the icon is already mapped.
            if (d->icon->surface && wlr_surface_has_buffer(d->icon->surface))
            {
                this->drag_icon->force_map();
            }
        }

        this->drag_active = true;

        wf::dnd_signal evdata;
        evdata.icon = this->drag_icon.get();
        wf::get_core().emit_signal("drag-started", &evdata);

        end_drag.set_callback([&] (void*)
        {
            wf::dnd_signal data;
            wf::get_core().emit_signal("drag-stopped", &data);
            this->drag_active = false;
            end_drag.disconnect();
        });
        end_drag.connect(&d->events.destroy);
    });
    start_drag.connect(&seat->events.start_drag);

    request_set_selection.set_callback([&] (void *data)
    {
        auto ev = static_cast<wlr_seat_request_set_selection_event*>(data);
        wlr_seat_set_selection(wf::get_core().get_current_seat(),
            ev->source, ev->serial);
    });
    request_set_selection.connect(&seat->events.request_set_selection);

    request_set_primary_selection.set_callback([&] (void *data)
    {
        auto ev =
            static_cast<wlr_seat_request_set_primary_selection_event*>(data);
        wlr_seat_set_primary_selection(wf::get_core().get_current_seat(),
            ev->source, ev->serial);
    });
    request_set_primary_selection.connect(
        &seat->events.request_set_primary_selection);

    on_new_device.set_callback([&] (signal_data_t *data)
    {
        auto ev = static_cast<input_device_signal*>(data);
        switch (ev->device->get_wlr_handle()->type)
        {
          case WLR_INPUT_DEVICE_KEYBOARD:
            this->keyboards.emplace_back(std::make_unique<wf::keyboard_t>(
                ev->device->get_wlr_handle()));
            if (this->current_keyboard == nullptr)
            {
                set_keyboard(keyboards.back().get());
            }

            break;

          case WLR_INPUT_DEVICE_TOUCH:
          case WLR_INPUT_DEVICE_POINTER:
          case WLR_INPUT_DEVICE_TABLET_TOOL:
            this->cursor->add_new_device(ev->device->get_wlr_handle());
            break;

          default:
            break;
        }

        update_capabilities();
    });

    on_remove_device.set_callback([&] (signal_data_t *data)
    {
        auto dev =
            static_cast<input_device_signal*>(data)->device->get_wlr_handle();
        if (dev->type == WLR_INPUT_DEVICE_KEYBOARD)
        {
            bool current_kbd_destroyed = false;
            if (current_keyboard && (current_keyboard->device == dev))
            {
                current_kbd_destroyed = true;
            }

            auto it = std::remove_if(keyboards.begin(), keyboards.end(),
                [=] (const std::unique_ptr<wf::keyboard_t>& kbd)
            {
                return kbd->device == dev;
            });

            keyboards.erase(it, keyboards.end());

            if (current_kbd_destroyed && keyboards.size())
            {
                set_keyboard(keyboards.front().get());
            } else
            {
                set_keyboard(nullptr);
            }
        }

        update_capabilities();
    });
    wf::get_core().connect_signal("input-device-added", &on_new_device);
    wf::get_core().connect_signal("input-device-removed", &on_remove_device);
}

void wf::seat_t::update_capabilities()
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

void wf::seat_t::validate_drag_request(wlr_seat_request_start_drag_event *ev)
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

void wf::seat_t::update_drag_icon()
{
    if (drag_icon && drag_icon->is_mapped())
    {
        drag_icon->damage();
    }
}

void wf::seat_t::set_keyboard(wf::keyboard_t *keyboard)
{
    this->current_keyboard = keyboard;
    wlr_seat_set_keyboard(seat,
        keyboard ? wlr_keyboard_from_input_device(keyboard->device) : NULL);
}

void wf::seat_t::break_mod_bindings()
{
    for (auto& kbd : this->keyboards)
    {
        kbd->mod_binding_key = 0;
    }
}

uint32_t wf::seat_t::get_modifiers()
{
    return current_keyboard ? current_keyboard->get_modifiers() : 0;
}

void wf::seat_t::set_keyboard_focus(wayfire_view view)
{
    auto surface = view ? view->get_keyboard_focus_surface() : NULL;
    auto iv  = interactive_view_from_view(view.get());
    auto oiv = interactive_view_from_view(keyboard_focus.get());

    if (oiv)
    {
        oiv->handle_keyboard_leave();
    }

    if (iv)
    {
        iv->handle_keyboard_enter();
    }

    /* Don't focus if we have an active grab */
    if (!wf::get_core_impl().input->active_grab)
    {
        if (surface)
        {
            auto kbd = wlr_seat_get_keyboard(seat);
            wlr_seat_keyboard_notify_enter(seat, surface,
                kbd ? kbd->keycodes : NULL,
                kbd ? kbd->num_keycodes : 0,
                kbd ? &kbd->modifiers : NULL);
        } else
        {
            wlr_seat_keyboard_notify_clear_focus(seat);
        }

        keyboard_focus = view;
    } else
    {
        wlr_seat_keyboard_notify_clear_focus(seat);
        keyboard_focus = nullptr;
    }

    wf::keyboard_focus_changed_signal data;
    data.view    = view;
    data.surface = surface;
    wf::get_core().emit_signal("keyboard-focus-changed", &data);
}

void wf::seat_t::ensure_input_surface(wf::surface_interface_t *surface)
{
    if (!surface || !surface->get_wlr_surface())
    {
        last_focus_surface = nullptr;
        return;
    }

    auto wlr_surf = surface->get_wlr_surface();
    if (this->last_focus_surface == wlr_surf)
    {
        return;
    }

    this->last_focus_surface = wlr_surf;
    wf::xwayland_bring_to_front(wlr_surf);
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

wf::pointf_t get_surface_relative_coords(wf::surface_interface_t *surface,
    const wf::pointf_t& point)
{
    auto og    = surface->get_output()->get_layout_geometry();
    auto local = point;
    local.x -= og.x;
    local.y -= og.y;

    auto view =
        dynamic_cast<wf::view_interface_t*>(surface->get_main_surface());

    return view->global_to_local_point(local, surface);
}
