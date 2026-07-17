#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_shm;
struct wl_surface;
struct wl_buffer;
struct wl_seat;
struct wl_touch;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;

namespace wf::test
{
struct touch_event_t
{
    enum type_t
    {
        DOWN,
        UP,
        MOTION,
        CANCEL,
        FRAME,
    } type;

    int32_t id = -1;
    double x   = 0.0;
    double y   = 0.0;
};

class wayland_xdg_client_t
{
  public:
    explicit wayland_xdg_client_t(const std::string& socket_name);
    ~wayland_xdg_client_t();

    wayland_xdg_client_t(const wayland_xdg_client_t&) = delete;
    wayland_xdg_client_t(wayland_xdg_client_t&&) = delete;
    wayland_xdg_client_t& operator =(const wayland_xdg_client_t&) = delete;
    wayland_xdg_client_t& operator =(wayland_xdg_client_t&&) = delete;

    void roundtrip();
    void dispatch_once(int timeout_ms = 0);
    bool dispatch_until_configure(int max_iterations = 200);

    bool has_required_globals() const;
    bool has_touch() const;
    void create_toplevel(const std::string& title, const std::string& app_id);
    bool has_pending_configure() const;
    uint32_t last_configure_serial() const;
    std::optional<std::pair<int, int>> last_toplevel_size() const;
    bool last_toplevel_configure_fullscreen() const;
    int last_preferred_buffer_scale() const;
    std::optional<uint32_t> last_fractional_scale() const;
    void attach_with_fractional_scale(int surface_width, int surface_height);
    void attach_with_fractional_scale(int surface_width, int surface_height,
        const std::vector<uint32_t>& pixels);
    void ack_last_configure();
    void clear_pending_configure();
    void attach_and_commit(int width, int height);
    void attach_and_commit(int width, int height, const std::vector<uint32_t>& pixels);
    std::pair<int, int> last_committed_buffer_size() const;
    void commit_surface();
    void destroy_toplevel();
    const std::vector<touch_event_t>& touch_events() const;
    void clear_touch_events();

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}
