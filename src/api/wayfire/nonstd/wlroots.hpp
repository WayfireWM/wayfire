#pragma once

/**
 * This file is used to put all wlroots headers needed in the Wayfire API
 * in an extern "C" block because wlroots headers are not always compatible
 * with C++.
 */
extern "C"
{
    struct wlr_backend;
    struct wlr_renderer;
    struct wlr_seat;
    struct wlr_cursor;
    struct wlr_data_device_manager;
    struct wlr_data_control_manager_v1;
    struct wlr_gamma_control_manager_v1;
    struct wlr_xdg_output_manager_v1;
    struct wlr_export_dmabuf_manager_v1;
    struct wlr_server_decoration_manager;
    struct wlr_xdg_decoration_manager_v1;
    struct wlr_input_inhibit_manager;
    struct wlr_virtual_keyboard_manager_v1;
    struct wlr_virtual_pointer_manager_v1;
    struct wlr_idle;
    struct wlr_idle_inhibit_manager_v1;
    struct wlr_screencopy_manager_v1;
    struct wlr_foreign_toplevel_manager_v1;
    struct wlr_pointer_gestures_v1;
    struct wlr_relative_pointer_manager_v1;
    struct wlr_pointer_constraints_v1;
    struct wlr_tablet_manager_v2;
    struct wlr_input_method_manager_v2;
    struct wlr_text_input_manager_v3;
    struct wlr_presentation;
    struct wlr_primary_selection_v1_device_manager;

    struct wlr_xdg_foreign_v1;
    struct wlr_xdg_foreign_v2;
    struct wlr_xdg_foreign_registry;

    struct wlr_pointer_axis_event;
    struct wlr_pointer_motion_event;
    struct wlr_output_layout;
    struct wlr_surface;
    struct wlr_texture;
    struct wlr_viewporter;

#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#include <wayland-server.h>

    static constexpr uint32_t WLR_KEY_PRESSED  = WL_KEYBOARD_KEY_STATE_PRESSED;
    static constexpr uint32_t WLR_KEY_RELEASED = WL_KEYBOARD_KEY_STATE_RELEASED;
}
