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

    /**
     * Get the factor by which scrolling with the given axis source should be
     * scaled. Finger scrolling always uses touchpad_scroll_speed, even for
     * devices which libinput does not report as touchpads, for example when
     * the events are forwarded by a parent compositor. Touchpads use it for
     * their other scroll methods as well, which libinput reports as
     * WL_POINTER_AXIS_SOURCE_CONTINUOUS.
     */
    double get_scroll_speed(wl_pointer_axis_source source);

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

    /** Whether libinput reports the device as a touchpad. */
    bool is_touchpad = false;

    void reconfigure_device(std::shared_ptr<wf::config::section_t> device_section) override;
};
}

#endif /* end of include guard: WF_SEAT_POINTING_DEVICE_HPP */
