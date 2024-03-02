#include <wayfire/util/log.hpp>
#include "input-method-relay.hpp"
#include "../core-impl.hpp"
#include "../../view/view-impl.hpp"
#include "core/seat/seat-impl.hpp"
#include "wayfire/scene-operations.hpp"

#include <algorithm>
#include <wayland-server-core.h>

wf::input_method_relay::input_method_relay()
{
    on_text_input_new.set_callback([&] (void *data)
    {
        auto wlr_text_input = static_cast<wlr_text_input_v3*>(data);
        text_inputs.push_back(std::make_unique<wf::text_input>(this,
            wlr_text_input));
        // Sometimes text_input is created after the surface, so we failed to
        // set_focus when the surface is focused. Try once here.
        //
        // If no surface has been created, set_focus does nothing.
        //
        // Example apps (all GTK4): gnome-font-viewer, easyeffects
        auto& seat = wf::get_core_impl().seat;
        if (auto focus = seat->priv->keyboard_focus)
        {
            auto surface = wf::node_to_view(focus)->get_keyboard_focus_surface();

            if (surface && (wl_resource_get_client(wlr_text_input->resource) ==
                            wl_resource_get_client(surface->resource)))
            {
                wlr_text_input_v3_send_enter(wlr_text_input, surface);
            }
        }
    });

    on_input_method_new.set_callback([&] (void *data)
    {
        auto new_input_method = static_cast<wlr_input_method_v2*>(data);
        if (input_method != nullptr)
        {
            LOGI("Attempted to connect second input method");
            wlr_input_method_v2_send_unavailable(new_input_method);

            return;
        }

        LOGD("new input method connected");
        input_method = new_input_method;
        last_done_serial.reset();
        next_done_serial = 0;
        on_input_method_commit.connect(&input_method->events.commit);
        on_input_method_destroy.connect(&input_method->events.destroy);
        on_grab_keyboard.connect(&input_method->events.grab_keyboard);
        on_new_popup_surface.connect(&input_method->events.new_popup_surface);

        auto *text_input = find_focusable_text_input();
        if (text_input)
        {
            wlr_text_input_v3_send_enter(
                text_input->input,
                text_input->pending_focused_surface);
            text_input->set_pending_focused_surface(nullptr);
        }
    });

    on_input_method_commit.set_callback([&] (void *data)
    {
        auto evt_input_method = static_cast<wlr_input_method_v2*>(data);
        assert(evt_input_method == input_method);

        // When we switch focus, we send a done event to the IM.
        // The IM may need time to process further events and may send additional commits after switching
        // focus, which belong to the old text input.
        //
        // To prevent this, we simply ignore commits which do not acknowledge the newest done event from the
        // compositor.
        if (input_method->current_serial < last_done_serial.value_or(0))
        {
            LOGD("focus just changed, ignore input method commit");
            return;
        }

        auto *text_input = find_focused_text_input();
        if (text_input == nullptr)
        {
            return;
        }

        if (input_method->current.preedit.text)
        {
            wlr_text_input_v3_send_preedit_string(text_input->input,
                input_method->current.preedit.text,
                input_method->current.preedit.cursor_begin,
                input_method->current.preedit.cursor_end);
        }

        if (input_method->current.commit_text)
        {
            wlr_text_input_v3_send_commit_string(text_input->input,
                input_method->current.commit_text);
        }

        if (input_method->current.delete_.before_length ||
            input_method->current.delete_.after_length)
        {
            wlr_text_input_v3_send_delete_surrounding_text(text_input->input,
                input_method->current.delete_.before_length,
                input_method->current.delete_.after_length);
        }

        wlr_text_input_v3_send_done(text_input->input);
    });

    on_input_method_destroy.set_callback([&] (void *data)
    {
        auto evt_input_method = static_cast<wlr_input_method_v2*>(data);
        assert(evt_input_method == input_method);

        on_input_method_commit.disconnect();
        on_input_method_destroy.disconnect();
        on_grab_keyboard.disconnect();
        on_grab_keyboard_destroy.disconnect();
        on_new_popup_surface.disconnect();
        input_method  = nullptr;
        keyboard_grab = nullptr;

        auto *text_input = find_focused_text_input();
        if (text_input != nullptr)
        {
            /* keyboard focus is still there, keep the surface at hand in case the IM
             * returns */
            text_input->set_pending_focused_surface(text_input->input->
                focused_surface);
            wlr_text_input_v3_send_leave(text_input->input);
        }
    });

    on_grab_keyboard.set_callback([&] (void *data)
    {
        if (keyboard_grab != nullptr)
        {
            LOGW("Attempted to grab input method keyboard twice");
            return;
        }

        keyboard_grab = static_cast<wlr_input_method_keyboard_grab_v2*>(data);
        on_grab_keyboard_destroy.connect(&keyboard_grab->events.destroy);
    });

    on_grab_keyboard_destroy.set_callback([&] (void *data)
    {
        on_grab_keyboard_destroy.disconnect();
        keyboard_grab = nullptr;
    });

    on_new_popup_surface.set_callback([&] (void *data)
    {
        auto popup = static_cast<wlr_input_popup_surface_v2*>(data);
        popup_surfaces.push_back(wf::popup_surface::create(this, popup));
    });

    auto& core = wf::get_core();
    if (core.protocols.text_input && core.protocols.input_method)
    {
        on_text_input_new.connect(&wf::get_core().protocols.text_input->events.text_input);
        on_input_method_new.connect(&wf::get_core().protocols.input_method->events.input_method);
        wf::get_core().connect(&keyboard_focus_changed);
    }
}

void wf::input_method_relay::send_im_state(wlr_text_input_v3 *input)
{
    wlr_input_method_v2_send_surrounding_text(
        input_method,
        input->current.surrounding.text,
        input->current.surrounding.cursor,
        input->current.surrounding.anchor);
    wlr_input_method_v2_send_text_change_cause(
        input_method,
        input->current.text_change_cause);
    wlr_input_method_v2_send_content_type(input_method,
        input->current.content_type.hint,
        input->current.content_type.purpose);
    send_im_done();
}

void wf::input_method_relay::send_im_done()
{
    last_done_serial = next_done_serial;
    next_done_serial++;
    wlr_input_method_v2_send_done(input_method);
}

void wf::input_method_relay::disable_text_input(wlr_text_input_v3 *input)
{
    if (input_method == nullptr)
    {
        LOGI("Disabling text input, but input method is gone");

        return;
    }

    // Don't deactivate input method if the text input isn't in focus.
    // We may get several and posibly interwined enable/disable calls while
    // switching focus / closing windows; don't deactivate for the wrong one.
    auto focused_input = find_focused_text_input();
    if (!focused_input || (input != focused_input->input))
    {
        return;
    }

    wlr_input_method_v2_send_deactivate(input_method);
    send_im_state(input);
}

void wf::input_method_relay::remove_text_input(wlr_text_input_v3 *input)
{
    auto it = std::remove_if(text_inputs.begin(),
        text_inputs.end(),
        [&] (const auto & inp)
    {
        return inp->input == input;
    });
    text_inputs.erase(it, text_inputs.end());
}

void wf::input_method_relay::remove_popup_surface(wf::popup_surface *popup)
{
    auto it = std::remove_if(popup_surfaces.begin(),
        popup_surfaces.end(),
        [&] (const auto & suf)
    {
        return suf.get() == popup;
    });
    popup_surfaces.erase(it, popup_surfaces.end());
}

bool wf::input_method_relay::should_grab(wlr_keyboard *kbd)
{
    if ((keyboard_grab == nullptr) || !find_focused_text_input())
    {
        return false;
    }

    return !is_im_sent(kbd);
}

bool wf::input_method_relay::is_im_sent(wlr_keyboard *kbd)
{
    struct wlr_virtual_keyboard_v1 *virtual_keyboard = wlr_input_device_get_virtual_keyboard(&kbd->base);
    if (!virtual_keyboard)
    {
        return false;
    }

    // We have already identified the device as IM-based device
    auto device_impl = (wf::input_device_impl_t*)kbd->base.data;
    if (device_impl->is_im_keyboard)
    {
        return true;
    }

    if (this->input_method)
    {
        // This is a workaround because we do not have sufficient information to know which virtual keyboards
        // are connected to IMs
        auto im_client   = wl_resource_get_client(input_method->resource);
        auto vkbd_client = wl_resource_get_client(virtual_keyboard->resource);

        if (im_client == vkbd_client)
        {
            device_impl->is_im_keyboard = true;
            return true;
        }
    }

    return false;
}

bool wf::input_method_relay::handle_key(struct wlr_keyboard *kbd, uint32_t time, uint32_t key,
    uint32_t state)
{
    if (!should_grab(kbd))
    {
        return false;
    }

    wlr_input_method_keyboard_grab_v2_set_keyboard(keyboard_grab, kbd);
    wlr_input_method_keyboard_grab_v2_send_key(keyboard_grab, time, key, state);
    return true;
}

bool wf::input_method_relay::handle_modifier(struct wlr_keyboard *kbd)
{
    if (!should_grab(kbd))
    {
        return false;
    }

    wlr_input_method_keyboard_grab_v2_set_keyboard(keyboard_grab, kbd);
    wlr_input_method_keyboard_grab_v2_send_modifiers(keyboard_grab, &kbd->modifiers);
    return true;
}

wf::text_input*wf::input_method_relay::find_focusable_text_input()
{
    auto it = std::find_if(text_inputs.begin(), text_inputs.end(),
        [&] (const auto & text_input)
    {
        return text_input->pending_focused_surface != nullptr;
    });
    if (it != text_inputs.end())
    {
        return it->get();
    }

    return nullptr;
}

wf::text_input*wf::input_method_relay::find_focused_text_input()
{
    auto it = std::find_if(text_inputs.begin(), text_inputs.end(),
        [&] (const auto & text_input)
    {
        return text_input->input->focused_surface != nullptr;
    });
    if (it != text_inputs.end())
    {
        return it->get();
    }

    return nullptr;
}

void wf::input_method_relay::set_focus(wlr_surface *surface)
{
    for (auto & text_input : text_inputs)
    {
        if (text_input->pending_focused_surface != nullptr)
        {
            assert(text_input->input->focused_surface == nullptr);
            if (surface != text_input->pending_focused_surface)
            {
                text_input->set_pending_focused_surface(nullptr);
            }
        } else if (text_input->input->focused_surface != nullptr)
        {
            assert(text_input->pending_focused_surface == nullptr);
            if (surface != text_input->input->focused_surface)
            {
                disable_text_input(text_input->input);
                wlr_text_input_v3_send_leave(text_input->input);
            } else
            {
                LOGD("set_focus an already focused surface");
                continue;
            }
        }

        if (surface && (wl_resource_get_client(text_input->input->resource) ==
                        wl_resource_get_client(surface->resource)))
        {
            if (input_method)
            {
                wlr_text_input_v3_send_enter(text_input->input, surface);
            } else
            {
                text_input->set_pending_focused_surface(surface);
            }
        }
    }
}

wf::input_method_relay::~input_method_relay()
{}

wf::text_input::text_input(wf::input_method_relay *rel, wlr_text_input_v3 *in) :
    relay(rel), input(in), pending_focused_surface(nullptr)
{
    on_text_input_enable.set_callback([&] (void *data)
    {
        auto wlr_text_input = static_cast<wlr_text_input_v3*>(data);
        assert(input == wlr_text_input);

        if (relay->input_method == nullptr)
        {
            LOGI("Enabling text input, but input method is gone");

            return;
        }

        wlr_input_method_v2_send_activate(relay->input_method);
        relay->send_im_state(input);
    });

    on_text_input_commit.set_callback([&] (void *data)
    {
        auto wlr_text_input = static_cast<wlr_text_input_v3*>(data);
        assert(input == wlr_text_input);

        if (!input->current_enabled)
        {
            LOGI("Inactive text input tried to commit");

            return;
        }

        if (relay->input_method == nullptr)
        {
            LOGI("Committing text input, but input method is gone");

            return;
        }

        for (auto popup : relay->popup_surfaces)
        {
            popup->update_geometry();
        }

        relay->send_im_state(input);
    });

    on_text_input_disable.set_callback([&] (void *data)
    {
        auto wlr_text_input = static_cast<wlr_text_input_v3*>(data);
        assert(input == wlr_text_input);

        relay->disable_text_input(input);
    });

    on_text_input_destroy.set_callback([&] (void *data)
    {
        auto wlr_text_input = static_cast<wlr_text_input_v3*>(data);
        assert(input == wlr_text_input);

        if (input->current_enabled)
        {
            relay->disable_text_input(wlr_text_input);
        }

        set_pending_focused_surface(nullptr);
        on_text_input_enable.disconnect();
        on_text_input_commit.disconnect();
        on_text_input_disable.disconnect();
        on_text_input_destroy.disconnect();

        // NOTE: the call destroys `this`
        relay->remove_text_input(wlr_text_input);
    });

    on_pending_focused_surface_destroy.set_callback([&] (void *data)
    {
        auto surface = static_cast<wlr_surface*>(data);
        assert(pending_focused_surface == surface);
        pending_focused_surface = nullptr;
        on_pending_focused_surface_destroy.disconnect();
    });

    on_text_input_enable.connect(&input->events.enable);
    on_text_input_commit.connect(&input->events.commit);
    on_text_input_disable.connect(&input->events.disable);
    on_text_input_destroy.connect(&input->events.destroy);
}

void wf::text_input::set_pending_focused_surface(wlr_surface *surface)
{
    pending_focused_surface = surface;

    if (surface == nullptr)
    {
        on_pending_focused_surface_destroy.disconnect();
    } else
    {
        on_pending_focused_surface_destroy.connect(&surface->events.destroy);
    }
}

wf::text_input::~text_input()
{}

wf::popup_surface::popup_surface(wf::input_method_relay *rel, wlr_input_popup_surface_v2 *in) :
    relay(rel), surface(in)
{
    main_surface = std::make_shared<wf::scene::wlr_surface_node_t>(in->surface, true);

    on_destroy.set_callback([&] (void*)
    {
        on_map.disconnect();
        on_unmap.disconnect();
        on_destroy.disconnect();

        relay->remove_popup_surface(this);
    });

    on_map.set_callback([&] (void*) { map(); });
    on_unmap.set_callback([&] (void*) { unmap(); });
    on_commit.set_callback([&] (void*) { update_geometry(); });

    on_map.connect(&surface->surface->events.map);
    on_unmap.connect(&surface->surface->events.unmap);
    on_destroy.connect(&surface->events.destroy);
}

std::shared_ptr<wf::popup_surface> wf::popup_surface::create(
    wf::input_method_relay *rel, wlr_input_popup_surface_v2 *in)
{
    auto self = view_interface_t::create<wf::popup_surface>(rel, in);
    auto translation_node = std::make_shared<wf::scene::translation_node_t>();
    translation_node->set_children_list({std::make_unique<wf::scene::wlr_surface_node_t>(in->surface,
        false)});
    self->surface_root_node = translation_node;
    self->set_surface_root_node(translation_node);
    self->set_role(VIEW_ROLE_DESKTOP_ENVIRONMENT);
    return self;
}

void wf::popup_surface::map()
{
    auto text_input = this->relay->find_focused_text_input();
    if (!text_input)
    {
        LOGE("trying to map IM popup surface without text input.");
        return;
    }

    auto view   = wf::wl_surface_to_wayfire_view(text_input->input->focused_surface->resource);
    auto output = view->get_output();
    if (!output)
    {
        LOGD("trying to map input method popup with a view not on an output.");
        return;
    }

    set_output(output);

    auto target_layer = wf::scene::layer::UNMANAGED;
    wf::scene::readd_front(get_output()->node_for_layer(target_layer), get_root_node());

    priv->set_mapped_surface_contents(main_surface);
    priv->set_mapped(true);
    _is_mapped = true;
    on_commit.connect(&surface->surface->events.commit);

    update_geometry();

    damage();
    emit_view_map();
}

void wf::popup_surface::unmap()
{
    if (!is_mapped())
    {
        return;
    }

    damage();

    priv->unset_mapped_surface_contents();

    emit_view_unmap();
    priv->set_mapped(false);
    _is_mapped = false;
    on_commit.disconnect();
}

std::string wf::popup_surface::get_app_id()
{
    return "input-method-popup";
}

std::string wf::popup_surface::get_title()
{
    return "input-method-popup";
}

void wf::popup_surface::update_geometry()
{
    auto text_input = this->relay->find_focused_text_input();
    if (!text_input)
    {
        LOGI("no focused text input");
        return;
    }

    if (!is_mapped())
    {
        LOGI("input method window not mapped");
        return;
    }

    bool cursor_rect = text_input->input->current.features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE;
    auto cursor = text_input->input->current.cursor_rectangle;
    int x = 0, y = 0;
    if (cursor_rect)
    {
        x = cursor.x;
        y = cursor.y + cursor.height;
    }

    auto wlr_surface = text_input->input->focused_surface;
    auto view = wf::wl_surface_to_wayfire_view(wlr_surface->resource);
    if (!view)
    {
        return;
    }

    damage();

    wf::pointf_t popup_offset = wf::place_popup_at(wlr_surface, surface->surface, {x* 1.0, y * 1.0});
    x = popup_offset.x;
    y = popup_offset.y;

    auto width  = surface->surface->current.width;
    auto height = surface->surface->current.height;

    auto output   = view->get_output();
    auto g_output = output->get_layout_geometry();
    // make sure right edge is on screen, sliding to the left when needed,
    // but keep left edge on screen nonetheless.
    x = std::max(0, std::min(x, g_output.width - width));
    // down edge is going to be out of screen; flip upwards
    if (y + height > g_output.height)
    {
        y -= height;
        if (cursor_rect)
        {
            y -= cursor.height;
        }
    }

    // make sure top edge is on screen, sliding down and sacrificing down edge if unavoidable
    y = std::max(0, y);

    surface_root_node->set_offset({x, y});
    geometry.x     = x;
    geometry.y     = y;
    geometry.width = width;
    geometry.height = height;
    damage();
    wf::scene::update(get_surface_root_node(), wf::scene::update_flag::GEOMETRY);
}

bool wf::popup_surface::is_mapped() const
{
    return priv->wsurface != nullptr && _is_mapped;
}

wf::geometry_t wf::popup_surface::get_geometry()
{
    return geometry;
}

wf::popup_surface::~popup_surface()
{}
