#include <wayfire/util/log.hpp>
#include "input-method-relay.hpp"
#include "../core-impl.hpp"
#include "../../output/output-impl.hpp"
#include "wayfire/scene-operations.hpp"

#include <algorithm>

wf::input_method_relay::input_method_relay()
{
    on_text_input_new.set_callback([&] (void *data)
    {
        auto wlr_text_input = static_cast<wlr_text_input_v3*>(data);
        text_inputs.push_back(std::make_unique<wf::text_input>(this,
            wlr_text_input));
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

        LOGI("new input method connected");
        input_method = new_input_method;
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

        // FIXME: workaround focus change while preediting
        if (focus_just_changed)
        {
            LOGI("focus_just_changed, ignore input method commit");
            focus_just_changed = false;
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
            LOGI("Attempted to grab input method keyboard twice");
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

    on_text_input_new.connect(&wf::get_core().protocols.text_input->events.text_input);
    on_input_method_new.connect(&wf::get_core().protocols.input_method->events.input_method);
    wf::get_core().connect(&keyboard_focus_changed);
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
    wlr_input_method_v2_send_done(input_method);
}

void wf::input_method_relay::disable_text_input(wlr_text_input_v3 *input)
{
    if (input_method == nullptr)
    {
        LOGI("Disabling text input, but input method is gone");

        return;
    }

    // on focus change, we disable on one text input and enable on another
    // however, the former may also send us a disable request AFTER we've done the above
    // let's just ignore repeated disable reuqests.
    if (input == already_disabled_input)
    {
        return;
    }

    wlr_input_method_v2_send_deactivate(input_method);
    send_im_state(input);
    already_disabled_input = input;
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
    if (keyboard_grab == nullptr)
    {
        return false;
    }

    // input method sends key via a virtual keyboard
    struct wlr_virtual_keyboard_v1 *virtual_keyboard = wlr_input_device_get_virtual_keyboard(&kbd->base);
    if (virtual_keyboard &&
        (wl_resource_get_client(virtual_keyboard->resource) ==
         wl_resource_get_client(input_method->keyboard_grab->resource)))
    {
        return false;
    }

    return true;
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
    set_output(output);

    auto target_layer = wf::scene::layer::UNMANAGED;
    wf::scene::readd_front(get_output()->node_for_layer(target_layer), get_root_node());

    priv->set_mapped_surface_contents(main_surface);
    priv->set_mapped(true);
    on_commit.connect(&surface->surface->events.commit);

    update_geometry();

    damage();
    emit_view_map();
}

void wf::popup_surface::unmap()
{
    damage();

    priv->unset_mapped_surface_contents();

    emit_view_unmap();
    priv->set_mapped(false);
    on_commit.disconnect();
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
    auto view     = wf::wl_surface_to_wayfire_view(wlr_surface->resource);
    auto toplevel = toplevel_cast(view);
    auto g = toplevel->get_geometry();
    auto margins = toplevel->toplevel()->current().margins;

    auto xdg_surface = wlr_xdg_surface_try_from_wlr_surface(wlr_surface);
    if (xdg_surface)
    {
        // substract shadows etc; test app: d-feet
        x -= xdg_surface->current.geometry.x;
        y -= xdg_surface->current.geometry.y;
    }

    damage();
    x += g.x + margins.left;
    y += g.y + margins.top;
    auto width  = surface->surface->current.width;
    auto height = surface->surface->current.height;

    auto output   = view->get_output();
    auto g_output = output->get_layout_geometry();
    x = std::max(0, std::min(x, g_output.width - width));
    if (y + height > g_output.height)
    {
        y -= height;
        if (cursor_rect)
        {
            y -= cursor.height;
        }
    }

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
    return priv->wsurface != nullptr;
}

wf::geometry_t wf::popup_surface::get_geometry()
{
    return geometry;
}

wf::popup_surface::~popup_surface()
{}
