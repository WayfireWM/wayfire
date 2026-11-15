#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayland-server-core.h>
#include <wayfire/core.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

// wlr/interfaces/wlr_pointer.h has no extern "C" guard of its own, see
// plugins/ipc/stipc.cpp for the same pattern.
extern "C" {
#include <wlr/interfaces/wlr_pointer.h>
}

#include "../../src/core/seat/pointing-device.hpp"
#include "../support/headless-core-harness.hpp"

namespace
{
static const wlr_pointer_impl test_pointer_impl = {
    .name = "wayfire-test-pointer",
};

class test_pointer_t
{
  public:
    test_pointer_t()
    {
        wlr_pointer_init(&pointer, &test_pointer_impl, "wayfire-test-pointer");
        wl_signal_emit_mutable(&wf::get_core().backend->events.new_input, &pointer.base);
    }

    test_pointer_t(const test_pointer_t&) = delete;
    test_pointer_t& operator =(const test_pointer_t&) = delete;

    ~test_pointer_t()
    {
        wlr_pointer_finish(&pointer);
    }

    /**
     * Mark the device as a touchpad, the way libinput would report a real one.
     * A dummy device is not a libinput device, so the compositor cannot detect
     * this on its own.
     */
    void mark_as_touchpad()
    {
        auto device = static_cast<wf::pointing_device_t*>(pointer.base.data);
        REQUIRE(device);
        device->is_touchpad = true;
    }

    wlr_pointer pointer = {};
};
}

TEST_CASE("scroll speed follows both the axis source and the device type")
{
    wf::test::headless_core_harness_t harness{
        "[input]\n"
        "mouse_scroll_speed = 0.5\n"
        "touchpad_scroll_speed = 0.1\n"};

    test_pointer_t mouse;
    test_pointer_t touchpad;
    touchpad.mark_as_touchpad();

    // Finger scrolling uses the touchpad speed even when the device is not
    // known to be a touchpad, for example in a nested session.
    CHECK(harness.pointer_axis(&mouse.pointer, WL_POINTER_AXIS_SOURCE_FINGER,
        10.0) == doctest::Approx(1.0));

    CHECK(harness.pointer_axis(&mouse.pointer, WL_POINTER_AXIS_SOURCE_WHEEL,
        10.0) == doctest::Approx(5.0));

    // Button scrolling, which libinput reports as a continuous source, follows
    // the device: the touchpad speed on a touchpad, the mouse speed elsewhere.
    CHECK(harness.pointer_axis(&touchpad.pointer, WL_POINTER_AXIS_SOURCE_CONTINUOUS,
        10.0) == doctest::Approx(1.0));

    CHECK(harness.pointer_axis(&mouse.pointer, WL_POINTER_AXIS_SOURCE_CONTINUOUS,
        10.0) == doctest::Approx(5.0));

    CHECK(harness.pointer_axis(&touchpad.pointer, WL_POINTER_AXIS_SOURCE_FINGER,
        10.0) == doctest::Approx(1.0));
}
