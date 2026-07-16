#pragma once

#include <stdint.h>

struct wl_output;
struct wl_surface;
struct xdg_popup;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;
struct zwlr_layer_surface_v1_listener;

#ifdef __cplusplus
extern "C" {
#endif

struct zwlr_layer_surface_v1 *wf_test_layer_shell_get_layer_surface(
    struct zwlr_layer_shell_v1 *layer_shell,
    struct wl_surface *surface,
    struct wl_output *output,
    uint32_t layer,
    const char *namespace_name);

int wf_test_layer_surface_add_listener(
    struct zwlr_layer_surface_v1 *layer_surface,
    const struct zwlr_layer_surface_v1_listener *listener,
    void *data);

void wf_test_layer_shell_destroy(struct zwlr_layer_shell_v1 *layer_shell);
void wf_test_layer_surface_set_size(struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t width, uint32_t height);
void wf_test_layer_surface_set_anchor(struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t anchor);
void wf_test_layer_surface_set_keyboard_interactivity(
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t keyboard_interactivity);
void wf_test_layer_surface_set_exclusive_zone(
    struct zwlr_layer_surface_v1 *layer_surface,
    int32_t zone);
void wf_test_layer_surface_set_margin(struct zwlr_layer_surface_v1 *layer_surface,
    int32_t top, int32_t right, int32_t bottom, int32_t left);
void wf_test_layer_surface_get_popup(struct zwlr_layer_surface_v1 *layer_surface,
    struct xdg_popup *popup);
void wf_test_layer_surface_ack_configure(
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial);
void wf_test_layer_surface_destroy(struct zwlr_layer_surface_v1 *layer_surface);

#ifdef __cplusplus
}
#endif
