#ifndef WF_SEAT_SWITCH_HPP
#define WF_SEAT_SWITCH_HPP

#include "seat.hpp"

namespace wf
{
struct switch_device_t : public input_device_impl_t
{
    wf::wl_listener_wrapper on_switch;
    void handle_switched(wlr_switch_toggle_event *ev);

    switch_device_t(wlr_input_device *dev);
    virtual ~switch_device_t() = default;
};
}

#endif /* end of include guard: WF_SEAT_SWITCH_HPP */
