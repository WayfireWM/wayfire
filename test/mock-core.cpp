#include "mock-core.hpp"
#include <wayfire/output-layout.hpp>
#include <wayfire/touch/touch.hpp>

#include "../src/core/seat/seat.hpp"
#include "../src/core/seat/input-manager.hpp"
#include "../src/core/seat/pointer.hpp"
#include "../src/core/seat/input-method-relay.hpp"
#include "../src/core/seat/keyboard.hpp"
#include "../src/core/seat/cursor.hpp"
#include "../src/core/seat/touch.hpp"

// XXX: many of the functions in this file are empty
// They are not needed for the existing tests, but will have to be extended
// in the future.
void mock_core_t::init()
{}

void mock_core_t::post_init()
{}

void mock_core_t::shutdown()
{}

wf::compositor_state_t mock_core_t::get_current_state()
{
    return wf::compositor_state_t::UNKNOWN;
}

wlr_seat*mock_core_t::get_current_seat()
{
    return nullptr;
}

uint32_t mock_core_t::get_keyboard_modifiers()
{
    return 0;
}

void mock_core_t::set_cursor(std::string name)
{}

void mock_core_t::unhide_cursor()
{}

void mock_core_t::hide_cursor()
{}

void mock_core_t::warp_cursor(wf::pointf_t pos)
{}

wf::pointf_t mock_core_t::get_cursor_position()
{
    return {invalid_coordinate, invalid_coordinate};
}

wf::pointf_t mock_core_t::get_touch_position(int id)
{
    return {invalid_coordinate, invalid_coordinate};
}

wf::touch::gesture_state_t state;
const wf::touch::gesture_state_t& mock_core_t::get_touch_state()
{
    return ::state;
}

wf::surface_interface_t*mock_core_t::get_cursor_focus()
{
    return nullptr;
}

wf::surface_interface_t*mock_core_t::get_surface_at(
    wf::pointf_t point)
{
    return nullptr;
}

wf::surface_interface_t*mock_core_t::get_touch_focus()
{
    return nullptr;
}

void mock_core_t::add_touch_gesture(
    nonstd::observer_ptr<wf::touch::gesture_t> gesture)
{}

void mock_core_t::rem_touch_gesture(
    nonstd::observer_ptr<wf::touch::gesture_t> gesture)
{}

std::vector<nonstd::observer_ptr<wf::input_device_t>> mock_core_t::get_input_devices()
{
    return {};
}

wlr_cursor*mock_core_t::get_wlr_cursor()
{
    return nullptr;
}

void mock_core_t::focus_output(wf::output_t *wo)
{}

wf::output_t*mock_core_t::get_active_output()
{
    return nullptr;
}

int mock_core_t::focus_layer(uint32_t layer, int32_t request_uid_hint)
{
    return 0;
}

uint32_t mock_core_t::get_focused_layer()
{
    return 0;
}

void mock_core_t::unfocus_layer(int request)
{}

void mock_core_t::add_view(
    std::unique_ptr<wf::view_interface_t> view)
{}

std::vector<wayfire_view> mock_core_t::get_all_views()
{
    return {};
}

void mock_core_t::set_active_view(wayfire_view new_focus)
{}

void mock_core_t::focus_view(wayfire_view v)
{}

void mock_core_t::erase_view(wayfire_view v)
{}

wayfire_view mock_core_t::find_view(const std::string& id)
{
    if (fake_views.count(id))
    {
        return fake_views[id];
    }

    return nullptr;
}

pid_t mock_core_t::run(std::string command)
{
    return 0;
}

std::string mock_core_t::get_xwayland_display()
{
    return "";
}

void mock_core_t::move_view_to_output(wayfire_view v,
    wf::output_t *new_output, bool reconfigure)
{}

mock_core_t::mock_core_t()
{}
mock_core_t::~mock_core_t() = default;

wf::compositor_core_impl_t& wf::compositor_core_impl_t::get()
{
    return mock_core();
}

mock_core_t& mock_core()
{
    // Make everything use the mock implementation
    static mock_core_t mock;
    return mock;
}
