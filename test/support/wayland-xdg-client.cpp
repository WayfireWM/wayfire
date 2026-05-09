#include "wayland-xdg-client.hpp"

#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdexcept>
#include <string>
#include <vector>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "xdg-shell-client-protocol.h"

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

struct wf::test::wayland_xdg_client_t::impl
{
    wl_display *display   = nullptr;
    wl_registry *registry = nullptr;
    wl_compositor *compositor = nullptr;
    wl_shm *shm = nullptr;
    xdg_wm_base *wm_base = nullptr;

    wl_surface *surface = nullptr;
    ::xdg_surface *shell_surface   = nullptr;
    ::xdg_toplevel *shell_toplevel = nullptr;
    wl_buffer *buffer = nullptr;

    bool configured = false;
    uint32_t configure_serial = 0;

    static void handle_registry_global(void *data, wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version)
    {
        auto *self = static_cast<impl*>(data);
        if (std::string{interface} == wl_compositor_interface.name)
        {
            self->compositor = static_cast<wl_compositor*>(wl_registry_bind(registry,
                name, &wl_compositor_interface, 4));
        } else if (std::string{interface} == wl_shm_interface.name)
        {
            self->shm = static_cast<wl_shm*>(wl_registry_bind(registry,
                name, &wl_shm_interface, 1));
        } else if (std::string{interface} == xdg_wm_base_interface.name)
        {
            self->wm_base = static_cast<xdg_wm_base*>(wl_registry_bind(registry,
                name, &xdg_wm_base_interface, std::min(version, 2u)));
        }
    }

    static void handle_registry_remove(void*, wl_registry*, uint32_t)
    {}

    static constexpr wl_registry_listener registry_listener = {
        .global = handle_registry_global,
        .global_remove = handle_registry_remove,
    };

    static void handle_ping(void*, xdg_wm_base *wm_base, uint32_t serial)
    {
        xdg_wm_base_pong(wm_base, serial);
    }

    static constexpr xdg_wm_base_listener wm_base_listener = {
        .ping = handle_ping,
    };

    static void handle_xdg_surface_configure(void *data, ::xdg_surface *surface,
        uint32_t serial)
    {
        auto *self = static_cast<impl*>(data);
        self->configured = true;
        self->configure_serial = serial;
        xdg_surface_ack_configure(surface, serial);
    }

    static constexpr ::xdg_surface_listener xdg_surface_listener = {
        .configure = handle_xdg_surface_configure,
    };
};

wf::test::wayland_xdg_client_t::wayland_xdg_client_t(const std::string& socket_name)
{
    priv = std::make_unique<impl>();
    priv->display = wl_display_connect(socket_name.c_str());
    if (!priv->display)
    {
        throw std::runtime_error("Failed to connect test client to Wayland display");
    }

    priv->registry = wl_display_get_registry(priv->display);
    wl_registry_add_listener(priv->registry, &impl::registry_listener, priv.get());
    wl_display_flush(priv->display);
}

wf::test::wayland_xdg_client_t::~wayland_xdg_client_t()
{
    destroy_toplevel();
    if (priv->wm_base)
    {
        xdg_wm_base_destroy(priv->wm_base);
    }

    if (priv->registry)
    {
        wl_registry_destroy(priv->registry);
    }

    if (priv->shm)
    {
        wl_shm_destroy(priv->shm);
    }

    if (priv->compositor)
    {
        wl_compositor_destroy(priv->compositor);
    }

    if (priv->display)
    {
        wl_display_disconnect(priv->display);
    }
}

void wf::test::wayland_xdg_client_t::roundtrip()
{
    wl_display_roundtrip(priv->display);
}

void wf::test::wayland_xdg_client_t::dispatch_once(int timeout_ms)
{
    wl_display_dispatch_pending(priv->display);
    wl_display_flush(priv->display);

    if (wl_display_prepare_read(priv->display) != 0)
    {
        wl_display_dispatch_pending(priv->display);
        return;
    }

    pollfd pfd = {
        .fd     = wl_display_get_fd(priv->display),
        .events = POLLIN,
        .revents = 0,
    };

    if (poll(&pfd, 1, timeout_ms) > 0)
    {
        wl_display_read_events(priv->display);
        wl_display_dispatch_pending(priv->display);
    } else
    {
        wl_display_cancel_read(priv->display);
    }
}

bool wf::test::wayland_xdg_client_t::dispatch_until_configure(int max_iterations)
{
    for (int i = 0; i < max_iterations; ++i)
    {
        if (priv->configured)
        {
            return true;
        }

        dispatch_once(10);
    }

    return priv->configured;
}

bool wf::test::wayland_xdg_client_t::has_required_globals() const
{
    return priv->compositor && priv->shm && priv->wm_base;
}

void wf::test::wayland_xdg_client_t::create_toplevel(const std::string& title,
    const std::string& app_id)
{
    if (!has_required_globals())
    {
        throw std::runtime_error("Tried to create xdg toplevel before binding required globals");
    }

    xdg_wm_base_add_listener(priv->wm_base, &impl::wm_base_listener, priv.get());
    priv->surface = wl_compositor_create_surface(priv->compositor);
    priv->shell_surface = xdg_wm_base_get_xdg_surface(priv->wm_base, priv->surface);
    xdg_surface_add_listener(priv->shell_surface, &impl::xdg_surface_listener, priv.get());
    priv->shell_toplevel = xdg_surface_get_toplevel(priv->shell_surface);

    xdg_toplevel_set_title(priv->shell_toplevel, title.c_str());
    xdg_toplevel_set_app_id(priv->shell_toplevel, app_id.c_str());
    wl_surface_commit(priv->surface);
    wl_display_flush(priv->display);
}

bool wf::test::wayland_xdg_client_t::has_pending_configure() const
{
    return priv->configured;
}

uint32_t wf::test::wayland_xdg_client_t::last_configure_serial() const
{
    return priv->configure_serial;
}

void wf::test::wayland_xdg_client_t::ack_last_configure()
{
    if (!priv->configured)
    {
        throw std::runtime_error("Tried to ack configure before receiving one");
    }

    xdg_surface_ack_configure(priv->shell_surface, priv->configure_serial);
}

void wf::test::wayland_xdg_client_t::attach_and_commit(int width, int height)
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

    std::fill_n(static_cast<uint32_t*>(data), width * height, 0xff336699u);
    auto *pool = wl_shm_create_pool(priv->shm, fd, size);
    priv->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
        WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);

    wl_surface_attach(priv->surface, priv->buffer, 0, 0);
    wl_surface_damage_buffer(priv->surface, 0, 0, width, height);
    wl_surface_commit(priv->surface);
    wl_display_flush(priv->display);
}

void wf::test::wayland_xdg_client_t::commit_surface()
{
    wl_surface_commit(priv->surface);
    wl_display_flush(priv->display);
}

void wf::test::wayland_xdg_client_t::destroy_toplevel()
{
    if (priv->buffer)
    {
        wl_buffer_destroy(priv->buffer);
        priv->buffer = nullptr;
    }

    if (priv->shell_toplevel)
    {
        xdg_toplevel_destroy(priv->shell_toplevel);
        priv->shell_toplevel = nullptr;
    }

    if (priv->shell_surface)
    {
        xdg_surface_destroy(priv->shell_surface);
        priv->shell_surface = nullptr;
    }

    if (priv->surface)
    {
        wl_surface_destroy(priv->surface);
        priv->surface = nullptr;
    }

    if (priv->display)
    {
        wl_display_flush(priv->display);
    }
}
