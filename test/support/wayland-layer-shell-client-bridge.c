#include "wayland-layer-shell-client-bridge.h"

#include "wlr-layer-shell-client-protocol.h"

struct zwlr_layer_surface_v1 *wf_test_layer_shell_get_layer_surface(
    struct zwlr_layer_shell_v1 *layer_shell,
    struct wl_surface *surface,
    struct wl_output *output,
    uint32_t layer,
    const char *namespace_name)
{
    return zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, output,
        layer, namespace_name);
}

int wf_test_layer_surface_add_listener(
    struct zwlr_layer_surface_v1 *layer_surface,
    const struct zwlr_layer_surface_v1_listener *listener,
    void *data)
{
    return zwlr_layer_surface_v1_add_listener(layer_surface, listener, data);
}

void wf_test_layer_shell_destroy(struct zwlr_layer_shell_v1 *layer_shell)
{
    zwlr_layer_shell_v1_destroy(layer_shell);
}

void wf_test_layer_surface_set_size(struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t width, uint32_t height)
{
    zwlr_layer_surface_v1_set_size(layer_surface, width, height);
}

void wf_test_layer_surface_set_anchor(struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t anchor)
{
    zwlr_layer_surface_v1_set_anchor(layer_surface, anchor);
}

void wf_test_layer_surface_set_keyboard_interactivity(
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t keyboard_interactivity)
{
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface,
        keyboard_interactivity);
}

void wf_test_layer_surface_get_popup(struct zwlr_layer_surface_v1 *layer_surface,
    struct xdg_popup *popup)
{
    zwlr_layer_surface_v1_get_popup(layer_surface, popup);
}

void wf_test_layer_surface_ack_configure(
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial)
{
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}

void wf_test_layer_surface_destroy(struct zwlr_layer_surface_v1 *layer_surface)
{
    zwlr_layer_surface_v1_destroy(layer_surface);
}
