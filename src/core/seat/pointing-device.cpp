#include "pointing-device.hpp"

wf::pointing_device_t::pointing_device_t(wlr_input_device *dev) :
    input_device_impl_t(dev)
{
    update_options();
}

wf::pointing_device_t::config_t wf::pointing_device_t::config;
void wf::pointing_device_t::config_t::load()
{
    left_handed_mode.load_option("input/left_handed_mode");
    middle_emulation.load_option("input/middle_emulation");

    mouse_scroll_speed.load_option("input/mouse_scroll_speed");
    mouse_cursor_speed.load_option("input/mouse_cursor_speed");
    touchpad_cursor_speed.load_option("input/touchpad_cursor_speed");
    touchpad_scroll_speed.load_option("input/touchpad_scroll_speed");

    touchpad_tap_enabled.load_option("input/tap_to_click");
    touchpad_dwt_enabled.load_option("input/disable_touchpad_while_typing");
    touchpad_dwmouse_enabled.load_option("input/disable_touchpad_while_mouse");
    touchpad_natural_scroll_enabled.load_option("input/natural_scroll");

    mouse_accel_profile.load_option("input/mouse_accel_profile");
    touchpad_accel_profile.load_option("input/touchpad_accel_profile");

    touchpad_click_method.load_option("input/click_method");
    touchpad_scroll_method.load_option("input/scroll_method");
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

    libinput_device_config_left_handed_set(dev, config.left_handed_mode);

    libinput_device_config_middle_emulation_set_enabled(dev,
        config.middle_emulation ?
        LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED :
        LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);

    /* we are configuring a touchpad */
    if (libinput_device_config_tap_get_finger_count(dev) > 0)
    {
        libinput_device_config_accel_set_speed(dev,
            config.touchpad_cursor_speed);

        set_libinput_accel_profile(dev, config.touchpad_accel_profile);
        libinput_device_config_tap_set_enabled(dev,
            config.touchpad_tap_enabled ?
            LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);

        if ((std::string)config.touchpad_click_method == "default")
        {
            libinput_device_config_click_set_method(dev,
                libinput_device_config_click_get_default_method(dev));
        } else if ((std::string)config.touchpad_click_method == "none")
        {
            libinput_device_config_click_set_method(dev,
                LIBINPUT_CONFIG_CLICK_METHOD_NONE);
        } else if ((std::string)config.touchpad_click_method == "button-areas")
        {
            libinput_device_config_click_set_method(dev,
                LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
        } else if ((std::string)config.touchpad_click_method == "clickfinger")
        {
            libinput_device_config_click_set_method(dev,
                LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
        }

        if ((std::string)config.touchpad_scroll_method == "default")
        {
            libinput_device_config_scroll_set_method(dev,
                libinput_device_config_scroll_get_default_method(dev));
        } else if ((std::string)config.touchpad_scroll_method == "none")
        {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_NO_SCROLL);
        } else if ((std::string)config.touchpad_scroll_method == "two-finger")
        {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_2FG);
        } else if ((std::string)config.touchpad_scroll_method == "edge")
        {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_EDGE);
        } else if ((std::string)config.touchpad_scroll_method == "on-button-down")
        {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
        }

        libinput_device_config_dwt_set_enabled(dev,
            config.touchpad_dwt_enabled ?
            LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);

        libinput_device_config_send_events_set_mode(dev,
            config.touchpad_dwmouse_enabled ?
            LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE :
            LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);

        if (libinput_device_config_scroll_has_natural_scroll(dev) > 0)
        {
            libinput_device_config_scroll_set_natural_scroll_enabled(dev,
                (bool)config.touchpad_natural_scroll_enabled);
        }
    } else
    {
        libinput_device_config_accel_set_speed(dev,
            config.mouse_cursor_speed);
        set_libinput_accel_profile(dev, config.mouse_accel_profile);
    }
}
