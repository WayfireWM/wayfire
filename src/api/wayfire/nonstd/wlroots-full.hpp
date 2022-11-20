#pragma once

/**
 * This file is used to put all wlroots headers needed in the Wayfire
 * (not only API) in an extern "C" block because wlroots headers are not
 * always compatible with C++.
 *
 * Note that some wlroots headers require generated protocol header files.
 * There are disabled unless the protocol header file is present.
 */
#include <wayfire/nonstd/wlroots.hpp>

// WF_USE_CONFIG_H is set only when building Wayfire itself, external plugins
// need to use <wayfire/config.h>
#ifdef WF_USE_CONFIG_H
    #include <config.h>
#else
    #include <wayfire/config.h>
#endif

extern "C"
{
// Rendering
#define static
#include <wlr/types/wlr_compositor.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/gles2.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_matrix.h>
#undef static
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_viewporter.h>

#include <wlr/types/wlr_output_damage.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/util/region.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>

// Shells
#if  __has_include(<xdg-shell-protocol.h>)
    #include <wlr/types/wlr_xdg_shell.h>
    #include <wlr/types/wlr_xdg_decoration_v1.h>
#endif
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_server_decoration.h>

// layer-shell needs the protocol file, so we cannot expose it here
#if  __has_include(<wlr-layer-shell-unstable-v1-protocol.h>)
    #define namespace namespace_t
    #include <wlr/types/wlr_layer_shell_v1.h>
    #undef namespace
#endif

#if WF_HAS_XWAYLAND
    // We need to rename class to class_t for the xwayland definitions.
    // However, it indirectly includes pthread.h which uses 'class' in the
    // C++ meaning, so we should include pthread before overriding 'class'.
    #if __has_include(<pthread.h>)
        #include <pthread.h>
    #endif

    #define class class_t
    #define static
    #include <wlr/xwayland.h>
    #undef static
    #undef class
#endif

#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>

// Backends
#include <wlr/config.h>
#define static
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#if WLR_HAS_X11_BACKEND
    #include <wlr/backend/x11.h>
#endif
#include <wlr/backend/wayland.h>
#undef static
#include <wlr/backend/headless.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/session.h>
#include <wlr/util/log.h>

// Output management
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>

#if __has_include(<wlr-output-power-management-unstable-v1-protocol.h>)
    #include <wlr/types/wlr_output_power_management_v1.h>
#endif
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>

// Input
#include <wlr/types/wlr_seat.h>
#if __has_include(<pointer-constraints-unstable-v1-protocol.h>)
    #include <wlr/types/wlr_pointer_constraints_v1.h>
#endif
#include <wlr/types/wlr_cursor.h>
#if __has_include(<tablet-unstable-v2-protocol.h>)
    #include <wlr/types/wlr_tablet_v2.h>
#endif
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_switch.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/xcursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_input_inhibitor.h>
#define delete delete_
#include <wlr/types/wlr_input_method_v2.h>
#undef delete
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_primary_selection_v1.h>
}
