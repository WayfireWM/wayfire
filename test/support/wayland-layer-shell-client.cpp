#include "wayland-layer-shell-client.hpp"

#include <stdexcept>
#include <string>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#define namespace namespace_
#include "wlr-layer-shell-client-protocol.h"
#undef namespace

#include "wayland-client-utils.hpp"
#include "wayland-layer-shell-client-bridge.h"
#include "xdg-shell-client-protocol.h"

struct wf::test::wayland_layer_shell_client_t::impl
{
    wl_display *display   = nullptr;
    wl_registry *registry = nullptr;
    wl_compositor *compositor = nullptr;
    wl_shm *shm   = nullptr;
    wl_seat *seat = nullptr;
    xdg_wm_base *wm_base = nullptr;
    zwlr_layer_shell_v1 *layer_shell = nullptr;

    wl_surface *layer_surface = nullptr;
    zwlr_layer_surface_v1 *shell_layer_surface = nullptr;
    wl_buffer *layer_buffer = nullptr;

    wl_surface *popup_surface = nullptr;
    ::xdg_surface *popup_xdg_surface = nullptr;
    ::xdg_popup *shell_popup = nullptr;
    wl_buffer *popup_buffer  = nullptr;

    bool layer_configured = false;
    uint32_t layer_configure_serial = 0;

    bool popup_configured = false;
    uint32_t popup_configure_serial = 0;

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
        } else if (std::string{interface} == wl_seat_interface.name)
        {
            self->seat = static_cast<wl_seat*>(wl_registry_bind(registry,
                name, &wl_seat_interface, 5));
        } else if (std::string{interface} == xdg_wm_base_interface.name)
        {
            self->wm_base = static_cast<xdg_wm_base*>(wl_registry_bind(registry,
                name, &xdg_wm_base_interface, std::min(version, 2u)));
        } else if (std::string{interface} == zwlr_layer_shell_v1_interface.name)
        {
            self->layer_shell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(registry,
                name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
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

    static void handle_layer_surface_configure(void *data,
        zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t, uint32_t)
    {
        auto *self = static_cast<impl*>(data);
        self->layer_configured = true;
        self->layer_configure_serial = serial;
        wf_test_layer_surface_ack_configure(layer_surface, serial);
    }

    static void handle_layer_surface_closed(void*, zwlr_layer_surface_v1*)
    {}

    static constexpr zwlr_layer_surface_v1_listener layer_surface_listener = {
        .configure = handle_layer_surface_configure,
        .closed    = handle_layer_surface_closed,
    };

    static void handle_popup_xdg_surface_configure(void *data, ::xdg_surface *surface,
        uint32_t serial)
    {
        auto *self = static_cast<impl*>(data);
        self->popup_configured = true;
        self->popup_configure_serial = serial;
        xdg_surface_ack_configure(surface, serial);
    }

    static constexpr ::xdg_surface_listener popup_xdg_surface_listener = {
        .configure = handle_popup_xdg_surface_configure,
    };

    static void handle_popup_configure(void*, ::xdg_popup*, int32_t, int32_t,
        int32_t, int32_t)
    {}

    static void handle_popup_done(void*, ::xdg_popup*)
    {}

    static void handle_popup_repositioned(void*, ::xdg_popup*, uint32_t)
    {}

    static constexpr ::xdg_popup_listener popup_listener = {
        .configure    = handle_popup_configure,
        .popup_done   = handle_popup_done,
        .repositioned = handle_popup_repositioned,
    };
};

wf::test::wayland_layer_shell_client_t::wayland_layer_shell_client_t(const std::string& socket_name)
{
    priv = std::make_unique<impl>();
    priv->display = wl_display_connect(socket_name.c_str());
    if (!priv->display)
    {
        throw std::runtime_error("Failed to connect layer-shell test client to Wayland display");
    }

    priv->registry = wl_display_get_registry(priv->display);
    wl_registry_add_listener(priv->registry, &impl::registry_listener, priv.get());
    wl_display_flush(priv->display);
}

wf::test::wayland_layer_shell_client_t::~wayland_layer_shell_client_t()
{
    destroy_popup();
    destroy_layer_surface();

    if (priv->layer_shell)
    {
        wf_test_layer_shell_destroy(priv->layer_shell);
    }

    if (priv->wm_base)
    {
        xdg_wm_base_destroy(priv->wm_base);
    }

    if (priv->seat)
    {
        wl_seat_destroy(priv->seat);
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

void wf::test::wayland_layer_shell_client_t::dispatch_once(int timeout_ms)
{
    wayland_dispatch_once(priv->display, timeout_ms);
}

bool wf::test::wayland_layer_shell_client_t::has_required_globals() const
{
    return priv->compositor && priv->shm && priv->seat && priv->wm_base && priv->layer_shell;
}

void wf::test::wayland_layer_shell_client_t::set_layer_exclusive_zone(int zone)
{
    wf_test_layer_surface_set_exclusive_zone(priv->shell_layer_surface, zone);
}

void wf::test::wayland_layer_shell_client_t::set_layer_margin(int top, int right,
    int bottom, int left)
{
    wf_test_layer_surface_set_margin(priv->shell_layer_surface, top, right, bottom, left);
}

void wf::test::wayland_layer_shell_client_t::create_layer_surface(
    const std::string& namespace_name, uint32_t layer, uint32_t keyboard_interactivity,
    int width, int height, uint32_t anchor)
{
    if (!has_required_globals())
    {
        throw std::runtime_error("Tried to create layer surface before binding required globals");
    }

    xdg_wm_base_add_listener(priv->wm_base, &impl::wm_base_listener, priv.get());
    priv->layer_surface = wl_compositor_create_surface(priv->compositor);
    priv->shell_layer_surface = wf_test_layer_shell_get_layer_surface(priv->layer_shell,
        priv->layer_surface, nullptr, layer, namespace_name.c_str());
    wf_test_layer_surface_add_listener(priv->shell_layer_surface,
        &impl::layer_surface_listener, priv.get());
    wf_test_layer_surface_set_size(priv->shell_layer_surface, width, height);
    wf_test_layer_surface_set_anchor(priv->shell_layer_surface, anchor);
    wf_test_layer_surface_set_keyboard_interactivity(priv->shell_layer_surface,
        keyboard_interactivity);
    wl_surface_commit(priv->layer_surface);
    wl_display_flush(priv->display);
}

bool wf::test::wayland_layer_shell_client_t::has_pending_layer_configure() const
{
    return priv->layer_configured;
}

uint32_t wf::test::wayland_layer_shell_client_t::last_layer_configure_serial() const
{
    return priv->layer_configure_serial;
}

void wf::test::wayland_layer_shell_client_t::attach_layer_and_commit(int width, int height)
{
    if (priv->layer_buffer)
    {
        wl_buffer_destroy(priv->layer_buffer);
    }

    priv->layer_buffer = create_shm_buffer(priv->shm, width, height, 0xff6644aau);
    wl_surface_attach(priv->layer_surface, priv->layer_buffer, 0, 0);
    wl_surface_damage_buffer(priv->layer_surface, 0, 0, width, height);
    wl_surface_commit(priv->layer_surface);
    wl_display_flush(priv->display);
}

void wf::test::wayland_layer_shell_client_t::create_popup(int width, int height,
    uint32_t grab_serial)
{
    auto *positioner = xdg_wm_base_create_positioner(priv->wm_base);
    xdg_positioner_set_size(positioner, width, height);
    xdg_positioner_set_anchor_rect(positioner, 0, 0, width, height);
    xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);

    priv->popup_surface     = wl_compositor_create_surface(priv->compositor);
    priv->popup_xdg_surface = xdg_wm_base_get_xdg_surface(priv->wm_base, priv->popup_surface);
    xdg_surface_add_listener(priv->popup_xdg_surface, &impl::popup_xdg_surface_listener,
        priv.get());
    priv->shell_popup = xdg_surface_get_popup(priv->popup_xdg_surface, nullptr, positioner);
    xdg_popup_add_listener(priv->shell_popup, &impl::popup_listener, priv.get());
    wf_test_layer_surface_get_popup(priv->shell_layer_surface, priv->shell_popup);
    if (grab_serial)
    {
        xdg_popup_grab(priv->shell_popup, priv->seat, grab_serial);
    }

    xdg_positioner_destroy(positioner);
    wl_surface_commit(priv->popup_surface);
    wl_display_flush(priv->display);
}

bool wf::test::wayland_layer_shell_client_t::has_pending_popup_configure() const
{
    return priv->popup_configured;
}

void wf::test::wayland_layer_shell_client_t::attach_popup_and_commit(int width, int height)
{
    if (priv->popup_buffer)
    {
        wl_buffer_destroy(priv->popup_buffer);
    }

    priv->popup_buffer = create_shm_buffer(priv->shm, width, height, 0xff6644aau);
    wl_surface_attach(priv->popup_surface, priv->popup_buffer, 0, 0);
    wl_surface_damage_buffer(priv->popup_surface, 0, 0, width, height);
    wl_surface_commit(priv->popup_surface);
    wl_display_flush(priv->display);
}

void wf::test::wayland_layer_shell_client_t::destroy_popup()
{
    if (priv->popup_buffer)
    {
        wl_buffer_destroy(priv->popup_buffer);
        priv->popup_buffer = nullptr;
    }

    if (priv->shell_popup)
    {
        xdg_popup_destroy(priv->shell_popup);
        priv->shell_popup = nullptr;
    }

    if (priv->popup_xdg_surface)
    {
        xdg_surface_destroy(priv->popup_xdg_surface);
        priv->popup_xdg_surface = nullptr;
    }

    if (priv->popup_surface)
    {
        wl_surface_destroy(priv->popup_surface);
        priv->popup_surface = nullptr;
    }

    priv->popup_configured = false;
    priv->popup_configure_serial = 0;
    wl_display_flush(priv->display);
}

void wf::test::wayland_layer_shell_client_t::destroy_layer_surface()
{
    if (priv->layer_buffer)
    {
        wl_buffer_destroy(priv->layer_buffer);
        priv->layer_buffer = nullptr;
    }

    if (priv->shell_layer_surface)
    {
        wf_test_layer_surface_destroy(priv->shell_layer_surface);
        priv->shell_layer_surface = nullptr;
    }

    if (priv->layer_surface)
    {
        wl_surface_destroy(priv->layer_surface);
        priv->layer_surface = nullptr;
    }

    priv->layer_configured = false;
    priv->layer_configure_serial = 0;
    wl_display_flush(priv->display);
}
