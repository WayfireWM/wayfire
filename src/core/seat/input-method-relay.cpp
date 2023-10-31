#include <wayfire/util/log.hpp>
#include "input-method-relay.hpp"
#include "../core-impl.hpp"
#include "../../output/output-impl.hpp"

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
