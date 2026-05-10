#pragma once

#include <cstdint>
#include <vector>

struct wl_buffer;
struct wl_display;
struct wl_shm;

namespace wf::test
{
void wayland_dispatch_once(wl_display *display, int timeout_ms = 0);
wl_buffer *create_shm_buffer(wl_shm *shm, int width, int height, uint32_t color);
wl_buffer *create_shm_buffer(wl_shm *shm, int width, int height, const std::vector<uint32_t>& pixels);
}
