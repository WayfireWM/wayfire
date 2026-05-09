#include "wayland-client-utils.hpp"

#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <stdexcept>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

namespace
{
int create_shm_file(size_t size)
{
    char name[] = "/wayfire-test-XXXXXX";
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
    {
        return -1;
    }

    shm_unlink(name);
    if (ftruncate(fd, size) < 0)
    {
        close(fd);
        return -1;
    }

    return fd;
}
}

void wf::test::wayland_dispatch_once(wl_display *display, int timeout_ms)
{
    wl_display_dispatch_pending(display);
    wl_display_flush(display);

    if (wl_display_prepare_read(display) != 0)
    {
        wl_display_dispatch_pending(display);
        return;
    }

    pollfd pfd = {
        .fd     = wl_display_get_fd(display),
        .events = POLLIN,
        .revents = 0,
    };

    if (poll(&pfd, 1, timeout_ms) > 0)
    {
        wl_display_read_events(display);
        wl_display_dispatch_pending(display);
    } else
    {
        wl_display_cancel_read(display);
    }
}

wl_buffer*wf::test::create_shm_buffer(wl_shm *shm, int width, int height, uint32_t color)
{
    const int stride  = width * 4;
    const size_t size = stride * height;
    int fd = create_shm_file(size);
    if (fd < 0)
    {
        throw std::runtime_error("Failed to create shared memory file for test buffer");
    }

    void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
    {
        close(fd);
        throw std::runtime_error("Failed to mmap test buffer");
    }

    std::fill_n(static_cast<uint32_t*>(data), width * height, color);
    auto *pool   = wl_shm_create_pool(shm, fd, size);
    auto *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
        WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    return buffer;
}
