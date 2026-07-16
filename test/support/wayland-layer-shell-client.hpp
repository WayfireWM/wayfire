#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_shm;
struct wl_surface;
struct wl_buffer;
struct wl_seat;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_popup;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

namespace wf::test
{
class wayland_layer_shell_client_t
{
  public:
    explicit wayland_layer_shell_client_t(const std::string& socket_name);
    ~wayland_layer_shell_client_t();

    wayland_layer_shell_client_t(const wayland_layer_shell_client_t&) = delete;
    wayland_layer_shell_client_t(wayland_layer_shell_client_t&&) = delete;
    wayland_layer_shell_client_t& operator =(const wayland_layer_shell_client_t&) = delete;
    wayland_layer_shell_client_t& operator =(wayland_layer_shell_client_t&&) = delete;

    void dispatch_once(int timeout_ms = 0);
    bool has_required_globals() const;

    void create_layer_surface(const std::string& namespace_name, uint32_t layer,
        uint32_t keyboard_interactivity, int width, int height, uint32_t anchor);
    void set_layer_exclusive_zone(int zone);
    void set_layer_margin(int top, int right, int bottom, int left);
    bool has_pending_layer_configure() const;
    uint32_t last_layer_configure_serial() const;
    void attach_layer_and_commit(int width, int height);

    void create_popup(int width, int height, uint32_t grab_serial = 0);
    bool has_pending_popup_configure() const;
    void attach_popup_and_commit(int width, int height);

    void destroy_popup();
    void destroy_layer_surface();

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}
