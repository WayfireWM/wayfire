#include "pointing-device.hpp"
#include <wayfire/config-backend.hpp>

wf::pointing_device_t::pointing_device_t(wlr_input_device *dev) :
    input_device_impl_t(dev)
{
    dev->data = this;
    load_options();
    update_options();
}

void wf::pointing_device_t::load_options()
{
    auto section =
        wf::get_core().config_backend->get_input_device_section("input", get_wlr_handle());
    auto section_name = section->get_name();

    left_handed_mode.load_option(section_name + "/left_handed_mode");
    middle_emulation.load_option(section_name + "/middle_emulation");

    mouse_scroll_speed.load_option(section_name + "/mouse_scroll_speed");
    mouse_cursor_speed.load_option(section_name + "/mouse_cursor_speed");
    touchpad_cursor_speed.load_option(section_name + "/touchpad_cursor_speed");
    touchpad_scroll_speed.load_option(section_name + "/touchpad_scroll_speed");

    mouse_natural_scroll_enabled.load_option(section_name + "/mouse_natural_scroll");
    touchpad_tap_enabled.load_option(section_name + "/tap_to_click");
    touchpad_dwt_enabled.load_option(section_name + "/disable_touchpad_while_typing");
    touchpad_dwmouse_enabled.load_option(section_name + "/disable_touchpad_while_mouse");
    touchpad_natural_scroll_enabled.load_option(section_name + "/natural_scroll");
    touchpad_tap_and_drag_enabled.load_option(section_name + "/tap_and_drag");
    touchpad_drag_lock_enabled.load_option(section_name + "/drag_lock");
    touchpad_3fg_drag.load_option(section_name + "/3fg_drag");

    mouse_accel_profile.load_option(section_name + "/mouse_accel_profile");
    touchpad_accel_profile.load_option(section_name + "/touchpad_accel_profile");

    touchpad_click_method.load_option(section_name + "/click_method");
    touchpad_scroll_method.load_option(section_name + "/scroll_method");
}

static void set_libinput_accel_profile(libinput_device *dev, std::string name)
{
    if (name == "default")
    {
        libinput_device_config_accel_set_profile(dev,
            libinput_device_config_accel_get_default_profile(dev));
    } else if (name == "none")
    {
        libinput_device_config_accel_set_profile(dev,
            LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);
    } else if (name == "adaptive")
    {
        libinput_device_config_accel_set_profile(dev,
            LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
    } else if (name == "flat")
    {
        libinput_device_config_accel_set_profile(dev,
            LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
    } else
    {
        LOGE("Invalid libinput acceleration profile.");
    }
}

void wf::pointing_device_t::update_options()
{
    /* We currently support options only for libinput devices */
    if (!wlr_input_device_is_libinput(get_wlr_handle()))
    {
        return;
    }

    auto dev = wlr_libinput_get_device_handle(get_wlr_handle());
    assert(dev);

    libinput_device_config_left_handed_set(dev, left_handed_mode);

    libinput_device_config_middle_emulation_set_enabled(dev,
        middle_emulation ?
        LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED :
        LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);

    /* we are configuring a touchpad */
    if (libinput_device_config_tap_get_finger_count(dev) > 0)
    {
        libinput_device_config_accel_set_speed(dev,
            touchpad_cursor_speed);

        set_libinput_accel_profile(dev, touchpad_accel_profile);
        libinput_device_config_tap_set_enabled(dev,
            touchpad_tap_enabled ?
            LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);

        if ((std::string)touchpad_click_method == "default")
        {
            libinput_device_config_click_set_method(dev,
                libinput_device_config_click_get_default_method(dev));
        } else if ((std::string)touchpad_click_method == "none")
        {
            libinput_device_config_click_set_method(dev,
                LIBINPUT_CONFIG_CLICK_METHOD_NONE);
        } else if ((std::string)touchpad_click_method == "button-areas")
        {
            libinput_device_config_click_set_method(dev,
                LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
        } else if ((std::string)touchpad_click_method == "clickfinger")
        {
            libinput_device_config_click_set_method(dev,
                LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
        } else
        {
            LOGE("Invalid libinput click method.");
        }

        if ((std::string)touchpad_scroll_method == "default")
        {
            libinput_device_config_scroll_set_method(dev,
                libinput_device_config_scroll_get_default_method(dev));
        } else if ((std::string)touchpad_scroll_method == "none")
        {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_NO_SCROLL);
        } else if ((std::string)touchpad_scroll_method == "two-finger")
        {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_2FG);
        } else if ((std::string)touchpad_scroll_method == "edge")
        {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_EDGE);
        } else if ((std::string)touchpad_scroll_method == "on-button-down")
        {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
        } else
        {
            LOGE("Invalid libinput scroll method.");
        }

        libinput_device_config_dwt_set_enabled(dev,
            touchpad_dwt_enabled ?
            LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);

        libinput_device_config_send_events_set_mode(dev,
            touchpad_dwmouse_enabled ?
            LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE :
            LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);

        libinput_device_config_tap_set_drag_enabled(dev,
            touchpad_tap_and_drag_enabled ?
            LIBINPUT_CONFIG_DRAG_ENABLED :
            LIBINPUT_CONFIG_DRAG_DISABLED);

        libinput_device_config_tap_set_drag_lock_enabled(dev,
            touchpad_drag_lock_enabled ?
            LIBINPUT_CONFIG_DRAG_LOCK_ENABLED :
            LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);

        if ((std::string)touchpad_3fg_drag == "default")
        {
#if HAVE_LIBINPUT_3FG_DRAG
            libinput_device_config_3fg_drag_set_enabled(dev,
                libinput_device_config_3fg_drag_get_default_enabled(dev));
#endif
        } else if ((std::string)touchpad_3fg_drag == "none")
        {
#if HAVE_LIBINPUT_3FG_DRAG
            libinput_device_config_3fg_drag_set_enabled(dev,
                LIBINPUT_CONFIG_3FG_DRAG_DISABLED);
#else
            LOGW("Multi-finger drag not implemented with current libinput version.");
#endif
        } else if ((std::string)touchpad_3fg_drag == "3fg-drag")
        {
#if HAVE_LIBINPUT_3FG_DRAG
            libinput_device_config_3fg_drag_set_enabled(dev,
                LIBINPUT_CONFIG_3FG_DRAG_ENABLED_3FG);
#else
            LOGW("Multi-finger drag not implemented with current libinput version.");
#endif
        } else if ((std::string)touchpad_3fg_drag == "4fg-drag")
        {
#if HAVE_LIBINPUT_3FG_DRAG
            libinput_device_config_3fg_drag_set_enabled(dev,
                LIBINPUT_CONFIG_3FG_DRAG_ENABLED_4FG);
#else
            LOGW("Multi-finger drag not implemented with current libinput version.");
#endif
        } else
        {
            LOGE("Invalid libinput multi-finger drag value.");
        }

        if (libinput_device_config_scroll_has_natural_scroll(dev) > 0)
        {
            libinput_device_config_scroll_set_natural_scroll_enabled(dev,
                (bool)touchpad_natural_scroll_enabled);
        }
    } else
    {
        libinput_device_config_accel_set_speed(dev,
            mouse_cursor_speed);
        set_libinput_accel_profile(dev, mouse_accel_profile);

        if (libinput_device_config_scroll_has_natural_scroll(dev) > 0)
        {
            libinput_device_config_scroll_set_natural_scroll_enabled(dev,
                (bool)mouse_natural_scroll_enabled);
        }
    }
}

double wf::pointing_device_t::get_scroll_speed(wlr_input_device *dev, bool touchpad)
{
    if ((touchpad && (dev->type != WLR_INPUT_DEVICE_TABLET_PAD)) ||
        (!touchpad && (dev->type != WLR_INPUT_DEVICE_POINTER)))
    {
        return 1.0;
    }

    return touchpad ? touchpad_scroll_speed : mouse_scroll_speed;
}
