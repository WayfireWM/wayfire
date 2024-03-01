#include <cstring>
#include <wayfire/bindings-repository.hpp>
#include <linux/input-event-codes.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <wayfire/util/log.hpp>
#include "pointer.hpp"
#include "keyboard.hpp"
#include "../core-impl.hpp"
#include "touch.hpp"
#include "input-manager.hpp"
#include "input-method-relay.hpp"
#include "wayfire/signal-definitions.hpp"

void wf::keyboard_t::setup_listeners()
{
    on_config_reload = [=] (auto)
    {
        reload_input_options();
    };
    wf::get_core().connect(&on_config_reload);

    on_key.set_callback([&] (void *data)
    {
        auto ev    = static_cast<wlr_keyboard_key_event*>(data);
        auto mode  = emit_device_event_signal(ev);
        auto& seat = wf::get_core_impl().seat;

        if (mode == input_event_processing_mode_t::IGNORE)
        {
            wf::get_core().seat->notify_activity();
            emit_device_post_event_signal(ev);
            return;
        }

        auto is_im_sent = wf::get_core_impl().im_relay->is_im_sent(handle);
        if (!is_im_sent)
        {
            seat->priv->set_keyboard(this);
        }

        LOGC(IM, "Received key=", ev->keycode, " state=", ev->state, " im=", is_im_sent);
        if ((is_im_sent || !handle_keyboard_key(ev->keycode, ev->state)) &&
            (mode == input_event_processing_mode_t::FULL))
        {
            if (!is_im_sent &&
                wf::get_core_impl().im_relay->handle_key(handle, ev->time_msec, ev->keycode, ev->state))
            {
                LOGC(IM, "key=", ev->keycode, " state=", ev->state, " swallowed by IM.");
                return;
            }

            if (ev->state == WL_KEYBOARD_KEY_STATE_PRESSED)
            {
                seat->priv->pressed_keys.insert(ev->keycode);
            }

            if (ev->state == WL_KEYBOARD_KEY_STATE_RELEASED)
            {
                if (seat->priv->pressed_keys.count(ev->keycode))
                {
                    seat->priv->pressed_keys.erase(seat->priv->pressed_keys.find(ev->keycode));
                } else
                {
                    LOGC(IM, "Key ", ev->keycode, " ignored.");
                    return;
                }
            }

            // don't send IM sent keys to plugin grabs
            if (seat->priv->keyboard_focus && !(seat->priv->is_grab && is_im_sent))
            {
                LOGC(IM, "key=", ev->keycode, " state=", ev->state, " sent to node.");
                seat->priv->keyboard_focus->keyboard_interaction()
                    .handle_keyboard_key(wf::get_core().seat.get(), *ev);
            }
        } else
        {
            LOGC(IM, "key=", ev->keycode, " state=", ev->state, " swallowed by a plugin.");
        }

        wf::get_core().seat->notify_activity();
        emit_device_post_event_signal(ev);
    });

    on_modifier.set_callback([&] (void *data)
    {
        auto kbd  = static_cast<wlr_keyboard*>(data);
        auto seat = wf::get_core().get_current_seat();

        if (!wf::get_core_impl().im_relay->handle_modifier(kbd))
        {
            if (!wf::get_core_impl().im_relay->is_im_sent(handle))
            {
                wlr_seat_set_keyboard(seat, kbd);
            }

            wlr_seat_keyboard_send_modifiers(seat, &kbd->modifiers);
        }

        wf::get_core().seat->notify_activity();
    });

    on_key.connect(&handle->events.key);
    on_modifier.connect(&handle->events.modifiers);
}

wf::keyboard_t::keyboard_t(wlr_input_device *dev) :
    handle(wlr_keyboard_from_input_device(dev)), device(dev)
{
    model.load_option("input/xkb_model");
    variant.load_option("input/xkb_variant");
    layout.load_option("input/xkb_layout");
    options.load_option("input/xkb_options");
    rules.load_option("input/xkb_rules");

    repeat_rate.load_option("input/kb_repeat_rate");
    repeat_delay.load_option("input/kb_repeat_delay");

    // When the configuration options change, mark them as dirty.
    // They are applied at the config-reloaded signal.
    model.set_callback([=] () { this->dirty_options = true; });
    variant.set_callback([=] () { this->dirty_options = true; });
    layout.set_callback([=] () { this->dirty_options = true; });
    options.set_callback([=] () { this->dirty_options = true; });
    rules.set_callback([=] () { this->dirty_options = true; });
    repeat_rate.set_callback([=] () { this->dirty_options = true; });
    repeat_delay.set_callback([=] () { this->dirty_options = true; });

    setup_listeners();
    reload_input_options();
    wlr_seat_set_keyboard(
        wf::get_core().get_current_seat(), wlr_keyboard_from_input_device(dev));
}

uint32_t wf::keyboard_t::get_modifiers()
{
    return wlr_keyboard_get_modifiers(handle);
}

static void set_locked_mod(xkb_mod_mask_t *mods, xkb_keymap *keymap, const char *mod)
{
    xkb_mod_index_t mod_index = xkb_map_mod_get_index(keymap, mod);
    if (mod_index != XKB_MOD_INVALID)
    {
        *mods |= (uint32_t)1 << mod_index;
    }
}

void wf::keyboard_t::reload_input_options()
{
    if (!this->dirty_options)
    {
        return;
    }

    this->dirty_options = false;

    auto ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    /* Copy memory to stack, so that .c_str() is valid */
    std::string rules   = this->rules;
    std::string model   = this->model;
    std::string layout  = this->layout;
    std::string variant = this->variant;
    std::string options = this->options;

    xkb_rule_names names;
    names.rules   = rules.c_str();
    names.model   = model.c_str();
    names.layout  = layout.c_str();
    names.variant = variant.c_str();
    names.options = options.c_str();
    auto keymap = xkb_map_new_from_names(ctx, &names,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    if (!keymap)
    {
        LOGE("Could not create keymap with given configuration:",
            " rules=\"", rules, "\" model=\"", model, "\" layout=\"", layout,
            "\" variant=\"", variant, "\" options=\"", options, "\"");

        // reset to NULL
        std::memset(&names, 0, sizeof(names));
        keymap = xkb_map_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }

    xkb_mod_mask_t locked_mods = 0;

    if (wf::get_core_impl().input->locked_mods & KB_MOD_NUM_LOCK)
    {
        set_locked_mod(&locked_mods, keymap, XKB_MOD_NAME_NUM);
    }

    if (wf::get_core_impl().input->locked_mods & KB_MOD_CAPS_LOCK)
    {
        set_locked_mod(&locked_mods, keymap, XKB_MOD_NAME_CAPS);
    }

    wlr_keyboard_set_keymap(handle, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);

    wlr_keyboard_set_repeat_info(handle, repeat_rate, repeat_delay);

    wlr_keyboard_notify_modifiers(handle, 0, 0, locked_mods, 0);
}

wf::keyboard_t::~keyboard_t()
{}

static bool check_vt_switch(wlr_session *session, uint32_t key, uint32_t mods)
{
    if (!session)
    {
        return false;
    }

    if (mods ^ (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL))
    {
        return false;
    }

    if ((key < KEY_F1) || (key > KEY_F10))
    {
        return false;
    }

    int target_vt = key - KEY_F1 + 1;
    wlr_session_change_vt(session, target_vt);

    return true;
}

uint32_t wf::keyboard_t::mod_from_key(uint32_t key)
{
    xkb_keycode_t keycode = key + 8;

    const xkb_keysym_t *keysyms;
    auto keysyms_len = xkb_state_key_get_syms(handle->xkb_state, keycode, &keysyms);

    for (int i = 0; i < keysyms_len; i++)
    {
        auto key = keysyms[i];
        if ((key == XKB_KEY_Alt_L) || (key == XKB_KEY_Alt_R))
        {
            return WLR_MODIFIER_ALT;
        }

        if ((key == XKB_KEY_Control_L) || (key == XKB_KEY_Control_R))
        {
            return WLR_MODIFIER_CTRL;
        }

        if ((key == XKB_KEY_Shift_L) || (key == XKB_KEY_Shift_R))
        {
            return WLR_MODIFIER_SHIFT;
        }

        if ((key == XKB_KEY_Super_L) || (key == XKB_KEY_Super_R))
        {
            return WLR_MODIFIER_LOGO;
        }
    }

    return 0;
}

uint32_t wf::keyboard_t::get_locked_mods()
{
    uint32_t leds = 0;
    for (uint32_t i = 0; i < WLR_LED_COUNT; i++)
    {
        bool led_active = xkb_state_led_index_is_active(
            handle->xkb_state, handle->led_indexes[i]);
        if (led_active)
        {
            leds |= (1 << i);
        }
    }

    uint32_t mods = 0;
    if (leds & WLR_LED_NUM_LOCK)
    {
        mods |= KB_MOD_NUM_LOCK;
    }

    if (leds & WLR_LED_CAPS_LOCK)
    {
        mods |= KB_MOD_CAPS_LOCK;
    }

    return mods;
}

bool wf::keyboard_t::has_only_modifiers()
{
    for (size_t i = 0; i < handle->num_keycodes; i++)
    {
        if (!this->mod_from_key(handle->keycodes[i]))
        {
            return false;
        }
    }

    return true;
}

bool wf::keyboard_t::handle_keyboard_key(uint32_t key, uint32_t state)
{
    using namespace std::chrono;

    auto& input = wf::get_core_impl().input;
    auto& seat  = wf::get_core_impl().seat;

    bool handled_in_plugin = false;
    auto mod = mod_from_key(key);
    input->locked_mods = this->get_locked_mods();

    if (state == WLR_KEY_PRESSED)
    {
        if (check_vt_switch(wf::get_core().session, key, get_modifiers()))
        {
            return true;
        }

        bool modifiers_only = !seat->priv->lpointer->has_pressed_buttons() &&
            (seat->priv->touch->get_state().fingers.empty()) &&
            this->has_only_modifiers();

        /* as long as we have pressed only modifiers, we should check for modifier
         * bindings on release */
        if (mod && modifiers_only)
        {
            mod_binding_start = steady_clock::now();
            mod_binding_key   = key;
        } else
        {
            mod_binding_key = 0;
        }

        handled_in_plugin |= wf::get_core().bindings->handle_key(
            wf::keybinding_t{get_modifiers(), key}, mod_binding_key);
    } else
    {
        if (mod_binding_key != 0)
        {
            int timeout = wf::option_wrapper_t<int>(
                "input/modifier_binding_timeout");
            auto time_elapsed = duration_cast<milliseconds>(
                steady_clock::now() - mod_binding_start);

            if ((timeout <= 0) || (time_elapsed < milliseconds(timeout)))
            {
                wf::get_core().bindings->handle_key(
                    wf::keybinding_t{get_modifiers() | mod, 0}, mod_binding_key);
            }
        }

        mod_binding_key = 0;
    }

    return handled_in_plugin;
}
