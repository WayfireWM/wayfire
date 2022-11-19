#include "cursor.hpp"
#include "pointer.hpp"
#include "touch.hpp"
#include "../core-impl.hpp"
#include "../../view/view-impl.hpp"
#include "input-manager.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/compositor-surface.hpp"
#include "wayfire/output-layout.hpp"
#include "tablet.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/util/log.hpp"

wf::cursor_t::cursor_t(wf::seat_t *seat)
{
    cursor     = wlr_cursor_create();
    this->seat = seat;

    wlr_cursor_attach_output_layout(cursor,
        wf::get_core().output_layout->get_handle());

    wlr_cursor_map_to_output(cursor, NULL);
    wlr_cursor_warp(cursor, NULL, cursor->x, cursor->y);
    init_xcursor();

    config_reloaded = [=] (wf::signal_data_t*)
    {
        init_xcursor();
    };

    wf::get_core().connect_signal("reload-config", &config_reloaded);

    request_set_cursor.set_callback([&] (void *data)
    {
        auto ev = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
        set_cursor(ev, true);
    });
    request_set_cursor.connect(&seat->seat->events.request_set_cursor);
}

void wf::cursor_t::add_new_device(wlr_input_device *dev)
{
    wlr_cursor_attach_input_device(cursor, dev);
}

void wf::cursor_t::setup_listeners()
{
    auto& core = wf::get_core_impl();

    /* Dispatch pointer events to the pointer_t */
    on_frame.set_callback([&] (void*)
    {
        seat->lpointer->handle_pointer_frame();
        wlr_idle_notify_activity(core.protocols.idle,
            core.get_current_seat());
    });
    on_frame.connect(&cursor->events.frame);

#define setup_passthrough_callback(evname) \
    on_ ## evname.set_callback([&] (void *data) { \
        set_touchscreen_mode(false); \
        auto ev   = static_cast<wlr_pointer_ ## evname ## _event*>(data); \
        auto mode = emit_device_event_signal("pointer_" #evname, ev); \
        seat->lpointer->handle_pointer_ ## evname(ev, mode); \
        wlr_idle_notify_activity(core.protocols.idle, core.get_current_seat()); \
        emit_device_event_signal("pointer_" #evname "_post", ev); \
    }); \
    on_ ## evname.connect(&cursor->events.evname);

    setup_passthrough_callback(button);
    setup_passthrough_callback(motion);
    setup_passthrough_callback(motion_absolute);
    setup_passthrough_callback(axis);
    setup_passthrough_callback(swipe_begin);
    setup_passthrough_callback(swipe_update);
    setup_passthrough_callback(swipe_end);
    setup_passthrough_callback(pinch_begin);
    setup_passthrough_callback(pinch_update);
    setup_passthrough_callback(pinch_end);
#undef setup_passthrough_callback

    /**
     * All tablet events are directly sent to the tablet device, it should
     * manage them
     */
#define setup_tablet_callback(evname) \
    on_tablet_ ## evname.set_callback([&] (void *data) { \
        set_touchscreen_mode(false); \
        auto ev = static_cast<wlr_tablet_tool_ ## evname ## _event*>(data); \
        auto handling_mode = emit_device_event_signal("tablet_" #evname, ev); \
        if (ev->tablet->data) { \
            auto tablet = \
                static_cast<wf::tablet_t*>(ev->tablet->data); \
            tablet->handle_ ## evname(ev, handling_mode); \
        } \
        wlr_idle_notify_activity(wf::get_core().protocols.idle, seat->seat); \
        emit_device_event_signal("tablet_" #evname "_post", ev); \
    }); \
    on_tablet_ ## evname.connect(&cursor->events.tablet_tool_ ## evname);

    setup_tablet_callback(tip);
    setup_tablet_callback(axis);
    setup_tablet_callback(button);
    setup_tablet_callback(proximity);
#undef setup_tablet_callback
}

void wf::cursor_t::init_xcursor()
{
    std::string theme = wf::option_wrapper_t<std::string>("input/cursor_theme");
    int size = wf::option_wrapper_t<int>("input/cursor_size");
    auto theme_ptr = (theme == "default") ? NULL : theme.c_str();

    // Set environment variables needed for Xwayland and maybe other apps
    // which use them to determine the correct cursor size
    setenv("XCURSOR_SIZE", std::to_string(size).c_str(), 1);
    if (theme_ptr)
    {
        setenv("XCURSOR_THEME", theme_ptr, 1);
    }

    if (xcursor)
    {
        wlr_xcursor_manager_destroy(xcursor);
    }

    xcursor = wlr_xcursor_manager_create(theme_ptr, size);

    load_xcursor_scale(1);
    for (auto& wo : wf::get_core().output_layout->get_current_configuration())
    {
        if (wo.second.source & wf::OUTPUT_IMAGE_SOURCE_SELF)
        {
            load_xcursor_scale(wo.first->scale);
        }
    }

    set_cursor("default");
}

void wf::cursor_t::load_xcursor_scale(float scale)
{
    wlr_xcursor_manager_load(xcursor, scale);
}

void wf::cursor_t::set_cursor(std::string name)
{
    if (this->hide_ref_counter)
    {
        return;
    }

    if (this->touchscreen_mode_active)
    {
        return;
    }

    if (name == "default")
    {
        name = "left_ptr";
    }

    idle_set_cursor.run_once([name, this] ()
    {
        wlr_xcursor_manager_set_cursor_image(xcursor, name.c_str(), cursor);
    });
}

void wf::cursor_t::unhide_cursor()
{
    if (this->hide_ref_counter && --this->hide_ref_counter)
    {
        return;
    }

    set_cursor("default");
}

void wf::cursor_t::hide_cursor()
{
    idle_set_cursor.disconnect();
    wlr_cursor_set_surface(cursor, NULL, 0, 0);
    this->hide_ref_counter++;
}

void wf::cursor_t::warp_cursor(wf::pointf_t point)
{
    wlr_cursor_warp_closest(cursor, NULL, point.x, point.y);
}

wf::pointf_t wf::cursor_t::get_cursor_position()
{
    return {cursor->x, cursor->y};
}

void wf::cursor_t::set_cursor(
    wlr_seat_pointer_request_set_cursor_event *ev, bool validate_request)
{
    if (this->hide_ref_counter)
    {
        return;
    }

    if (this->touchscreen_mode_active)
    {
        return;
    }

    if (validate_request)
    {
        auto pointer_client = seat->seat->pointer_state.focused_client;
        if (pointer_client != ev->seat_client)
        {
            return;
        }
    }

    if (!wf::get_core_impl().input->input_grabbed())
    {
        wlr_cursor_set_surface(cursor, ev->surface,
            ev->hotspot_x, ev->hotspot_y);
    }
}

void wf::cursor_t::set_touchscreen_mode(bool enabled)
{
    if (this->touchscreen_mode_active == enabled)
    {
        return;
    }

    this->touchscreen_mode_active = enabled;
    if (enabled)
    {
        hide_cursor();
    } else
    {
        unhide_cursor();
    }
}

wf::cursor_t::~cursor_t()
{
    wf::get_core().disconnect_signal("reload-config", &config_reloaded);
}
