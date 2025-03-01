#ifndef WF_SEAT_POINTING_DEVICE_HPP
#define WF_SEAT_POINTING_DEVICE_HPP

#include "seat-impl.hpp"

namespace wf
{
struct pointing_device_t : public input_device_impl_t
{
    pointing_device_t(wlr_input_device *dev);
    virtual ~pointing_device_t() = default;

    void load_options();
    void update_options() override;

    double get_scroll_speed(wlr_input_device *dev, bool touchpad);

    wf::option_wrapper_t<bool> left_handed_mode;
    wf::option_wrapper_t<bool> middle_emulation;
    wf::option_wrapper_t<double> mouse_cursor_speed;
    wf::option_wrapper_t<double> mouse_scroll_speed;
    wf::option_wrapper_t<std::string> tablet_motion_mode;
    wf::option_wrapper_t<double> touchpad_cursor_speed;
    wf::option_wrapper_t<double> touchpad_scroll_speed;
    wf::option_wrapper_t<std::string> touchpad_click_method;
    wf::option_wrapper_t<std::string> touchpad_scroll_method;
    wf::option_wrapper_t<std::string> touchpad_accel_profile;
    wf::option_wrapper_t<std::string> mouse_accel_profile;
    wf::option_wrapper_t<bool> touchpad_tap_enabled;
    wf::option_wrapper_t<bool> touchpad_dwt_enabled;
    wf::option_wrapper_t<bool> touchpad_dwmouse_enabled;
    wf::option_wrapper_t<bool> touchpad_natural_scroll_enabled;
    wf::option_wrapper_t<bool> mouse_natural_scroll_enabled;
    wf::option_wrapper_t<bool> touchpad_tap_and_drag_enabled;
    wf::option_wrapper_t<bool> touchpad_drag_lock_enabled;
    wf::option_wrapper_t<std::string> touchpad_3fg_drag;
};
}

#endif /* end of include guard: WF_SEAT_POINTING_DEVICE_HPP */
