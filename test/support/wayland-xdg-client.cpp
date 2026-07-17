#include "wayland-xdg-client.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <cmath>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wayland-client-utils.hpp"
#include "xdg-shell-client-protocol.h"

struct wf::test::wayland_xdg_client_t::impl
{
    wl_display *display   = nullptr;
    wl_registry *registry = nullptr;
    wl_compositor *compositor = nullptr;
    wl_shm *shm     = nullptr;
    wl_seat *seat   = nullptr;
    wl_touch *touch = nullptr;
    xdg_wm_base *wm_base = nullptr;
    wp_fractional_scale_manager_v1 *fractional_scale_manager = nullptr;
    wp_viewporter *viewporter = nullptr;

    wl_surface *surface = nullptr;
    ::xdg_surface *shell_surface   = nullptr;
    ::xdg_toplevel *shell_toplevel = nullptr;
    wp_fractional_scale_v1 *fractional_scale = nullptr;
    wp_viewport *viewport = nullptr;
    wl_buffer *buffer     = nullptr;

    bool configured = false;
    uint32_t configure_serial = 0;
    std::optional<std::pair<int, int>> toplevel_size;
    bool toplevel_configure_fullscreen = false;
    int preferred_buffer_scale = 1;
    std::optional<uint32_t> preferred_fractional_scale;
    std::pair<int, int> committed_buffer_size = {0, 0};
    std::vector<touch_event_t> touch_events;

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
                name, &wl_seat_interface, std::min(version, 7u)));
            wl_seat_add_listener(self->seat, &seat_listener, self);
        } else if (std::string{interface} == xdg_wm_base_interface.name)
        {
            self->wm_base = static_cast<xdg_wm_base*>(wl_registry_bind(registry,
                name, &xdg_wm_base_interface, std::min(version, 7u)));
        } else if (std::string{interface} == wp_fractional_scale_manager_v1_interface.name)
        {
            self->fractional_scale_manager =
                static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(registry,
                    name, &wp_fractional_scale_manager_v1_interface, 1));
        } else if (std::string{interface} == wp_viewporter_interface.name)
        {
            self->viewporter = static_cast<wp_viewporter*>(wl_registry_bind(registry,
                name, &wp_viewporter_interface, 1));
        }
    }

    static void handle_registry_remove(void*, wl_registry*, uint32_t)
    {}

    static constexpr wl_registry_listener registry_listener = {
        .global = handle_registry_global,
        .global_remove = handle_registry_remove,
    };

    static void handle_seat_capabilities(void *data, wl_seat *seat, uint32_t capabilities)
    {
        auto *self = static_cast<impl*>(data);
        if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) && !self->touch)
        {
            self->touch = wl_seat_get_touch(seat);
            wl_touch_add_listener(self->touch, &touch_listener, self);
        } else if (!(capabilities & WL_SEAT_CAPABILITY_TOUCH) && self->touch)
        {
            wl_touch_destroy(self->touch);
            self->touch = nullptr;
        }
    }

    static void handle_seat_name(void*, wl_seat*, const char*)
    {}

    static constexpr wl_seat_listener seat_listener = {
        .capabilities = handle_seat_capabilities,
        .name = handle_seat_name,
    };

    static void handle_touch_down(void *data, wl_touch*, uint32_t, uint32_t,
        wl_surface*, int32_t id, wl_fixed_t x, wl_fixed_t y)
    {
        auto *self = static_cast<impl*>(data);
        self->touch_events.push_back({touch_event_t::DOWN, id,
            wl_fixed_to_double(x), wl_fixed_to_double(y)});
    }

    static void handle_touch_up(void *data, wl_touch*, uint32_t, uint32_t, int32_t id)
    {
        auto *self = static_cast<impl*>(data);
        self->touch_events.push_back({touch_event_t::UP, id});
    }

    static void handle_touch_motion(void *data, wl_touch*, uint32_t,
        int32_t id, wl_fixed_t x, wl_fixed_t y)
    {
        auto *self = static_cast<impl*>(data);
        self->touch_events.push_back({touch_event_t::MOTION, id,
            wl_fixed_to_double(x), wl_fixed_to_double(y)});
    }

    static void handle_touch_frame(void *data, wl_touch*)
    {
        auto *self = static_cast<impl*>(data);
        self->touch_events.push_back({touch_event_t::FRAME});
    }

    static void handle_touch_cancel(void *data, wl_touch*)
    {
        auto *self = static_cast<impl*>(data);
        self->touch_events.push_back({touch_event_t::CANCEL});
    }

    static void handle_touch_shape(void*, wl_touch*, int32_t, wl_fixed_t, wl_fixed_t)
    {}

    static void handle_touch_orientation(void*, wl_touch*, int32_t, wl_fixed_t)
    {}

    static constexpr wl_touch_listener touch_listener = {
        .down   = handle_touch_down,
        .up     = handle_touch_up,
        .motion = handle_touch_motion,
        .frame  = handle_touch_frame,
        .cancel = handle_touch_cancel,
        .shape  = handle_touch_shape,
        .orientation = handle_touch_orientation,
    };

    static void handle_ping(void*, xdg_wm_base *wm_base, uint32_t serial)
    {
        xdg_wm_base_pong(wm_base, serial);
    }

    static constexpr xdg_wm_base_listener wm_base_listener = {
        .ping = handle_ping,
    };

    static void handle_surface_enter(void*, wl_surface*, wl_output*)
    {}

    static void handle_surface_leave(void*, wl_surface*, wl_output*)
    {}

    static void handle_preferred_buffer_scale(void *data, wl_surface*, int32_t factor)
    {
        auto *self = static_cast<impl*>(data);
        self->preferred_buffer_scale = factor;
    }

    static void handle_preferred_buffer_transform(void*, wl_surface*, uint32_t)
    {}

    static constexpr wl_surface_listener surface_listener = {
        .enter = handle_surface_enter,
        .leave = handle_surface_leave,
        .preferred_buffer_scale     = handle_preferred_buffer_scale,
        .preferred_buffer_transform = handle_preferred_buffer_transform,
    };

    static void handle_fractional_preferred_scale(void *data, wp_fractional_scale_v1*, uint32_t scale)
    {
        auto *self = static_cast<impl*>(data);
        self->preferred_fractional_scale = scale;
    }

    static constexpr wp_fractional_scale_v1_listener fractional_scale_listener = {
        .preferred_scale = handle_fractional_preferred_scale,
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

    static void handle_toplevel_configure(void *data, ::xdg_toplevel*, int32_t width,
        int32_t height, wl_array *states)
    {
        auto *self = static_cast<impl*>(data);
        self->toplevel_size = {{width, height}};
        self->toplevel_configure_fullscreen = false;

        auto *state = static_cast<uint32_t*>(states->data);
        const size_t count = states->size / sizeof(uint32_t);
        for (size_t i = 0; i < count; ++i)
        {
            if (state[i] == XDG_TOPLEVEL_STATE_FULLSCREEN)
            {
                self->toplevel_configure_fullscreen = true;
                break;
            }
        }
    }

    static void handle_toplevel_close(void*, ::xdg_toplevel*)
    {}

    static void handle_toplevel_configure_bounds(void*, ::xdg_toplevel*, int32_t, int32_t)
    {}

    static void handle_toplevel_wm_capabilities(void*, ::xdg_toplevel*, wl_array*)
    {}

    static constexpr ::xdg_toplevel_listener xdg_toplevel_listener = {
        .configure = handle_toplevel_configure,
        .close     = handle_toplevel_close,
        .configure_bounds = handle_toplevel_configure_bounds,
        .wm_capabilities  = handle_toplevel_wm_capabilities,
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
    if (priv->fractional_scale_manager)
    {
        wp_fractional_scale_manager_v1_destroy(priv->fractional_scale_manager);
    }

    if (priv->viewporter)
    {
        wp_viewporter_destroy(priv->viewporter);
    }

    if (priv->wm_base)
    {
        xdg_wm_base_destroy(priv->wm_base);
    }

    if (priv->touch)
    {
        wl_touch_destroy(priv->touch);
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

void wf::test::wayland_xdg_client_t::roundtrip()
{
    wl_display_roundtrip(priv->display);
}

void wf::test::wayland_xdg_client_t::dispatch_once(int timeout_ms)
{
    wayland_dispatch_once(priv->display, timeout_ms);
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
    return priv->compositor && priv->shm && priv->wm_base && priv->viewporter;
}

bool wf::test::wayland_xdg_client_t::has_touch() const
{
    return priv->touch;
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
    wl_surface_add_listener(priv->surface, &impl::surface_listener, priv.get());
    if (priv->fractional_scale_manager)
    {
        priv->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
            priv->fractional_scale_manager, priv->surface);
        wp_fractional_scale_v1_add_listener(priv->fractional_scale,
            &impl::fractional_scale_listener, priv.get());
    }

    priv->viewport = wp_viewporter_get_viewport(priv->viewporter, priv->surface);

    priv->shell_surface = xdg_wm_base_get_xdg_surface(priv->wm_base, priv->surface);
    xdg_surface_add_listener(priv->shell_surface, &impl::xdg_surface_listener, priv.get());
    priv->shell_toplevel = xdg_surface_get_toplevel(priv->shell_surface);
    xdg_toplevel_add_listener(priv->shell_toplevel, &impl::xdg_toplevel_listener, priv.get());

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

std::optional<std::pair<int, int>> wf::test::wayland_xdg_client_t::last_toplevel_size() const
{
    return priv->toplevel_size;
}

bool wf::test::wayland_xdg_client_t::last_toplevel_configure_fullscreen() const
{
    return priv->toplevel_configure_fullscreen;
}

int wf::test::wayland_xdg_client_t::last_preferred_buffer_scale() const
{
    return priv->preferred_buffer_scale;
}

std::optional<uint32_t> wf::test::wayland_xdg_client_t::last_fractional_scale() const
{
    return priv->preferred_fractional_scale;
}

void wf::test::wayland_xdg_client_t::attach_with_fractional_scale(int surface_width, int surface_height)
{
    if (!priv->preferred_fractional_scale)
    {
        throw std::runtime_error("No preferred fractional scale advertised yet");
    }

    const double scale = *priv->preferred_fractional_scale / 120.0;
    const auto round_half_away_from_zero = [] (double value)
    {
        return int(std::copysign(std::floor(std::abs(value) + 0.5), value));
    };

    const int buffer_width = round_half_away_from_zero(surface_width * scale);
    const int buffer_height = round_half_away_from_zero(surface_height * scale);

    wl_surface_set_buffer_scale(priv->surface, 1);
    wp_viewport_set_destination(priv->viewport, surface_width, surface_height);
    attach_and_commit(buffer_width, buffer_height);
}

void wf::test::wayland_xdg_client_t::attach_with_fractional_scale(int surface_width, int surface_height,
    const std::vector<uint32_t>& pixels)
{
    if (!priv->preferred_fractional_scale)
    {
        throw std::runtime_error("No preferred fractional scale advertised yet");
    }

    const double scale = *priv->preferred_fractional_scale / 120.0;
    const auto round_half_away_from_zero = [] (double value)
    {
        return int(std::copysign(std::floor(std::abs(value) + 0.5), value));
    };

    const int buffer_width = round_half_away_from_zero(surface_width * scale);
    const int buffer_height = round_half_away_from_zero(surface_height * scale);

    wl_surface_set_buffer_scale(priv->surface, 1);
    wp_viewport_set_destination(priv->viewport, surface_width, surface_height);
    attach_and_commit(buffer_width, buffer_height, pixels);
}

void wf::test::wayland_xdg_client_t::ack_last_configure()
{
    if (!priv->configured)
    {
        throw std::runtime_error("Tried to ack configure before receiving one");
    }

    xdg_surface_ack_configure(priv->shell_surface, priv->configure_serial);
}

void wf::test::wayland_xdg_client_t::clear_pending_configure()
{
    priv->configured = false;
    priv->toplevel_size.reset();
    priv->toplevel_configure_fullscreen = false;
    priv->preferred_fractional_scale.reset();
}

void wf::test::wayland_xdg_client_t::attach_and_commit(int width, int height)
{
    priv->buffer = create_shm_buffer(priv->shm, width, height, 0xff336699u);
    priv->committed_buffer_size = {width, height};

    wl_surface_attach(priv->surface, priv->buffer, 0, 0);
    wl_surface_damage_buffer(priv->surface, 0, 0, width, height);
    wl_surface_commit(priv->surface);
    wl_display_flush(priv->display);
}

void wf::test::wayland_xdg_client_t::attach_and_commit(int width, int height,
    const std::vector<uint32_t>& pixels)
{
    priv->buffer = create_shm_buffer(priv->shm, width, height, pixels);
    priv->committed_buffer_size = {width, height};

    wl_surface_attach(priv->surface, priv->buffer, 0, 0);
    wl_surface_damage_buffer(priv->surface, 0, 0, width, height);
    wl_surface_commit(priv->surface);
    wl_display_flush(priv->display);
}

std::pair<int, int> wf::test::wayland_xdg_client_t::last_committed_buffer_size() const
{
    return priv->committed_buffer_size;
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

    if (priv->viewport)
    {
        wp_viewport_destroy(priv->viewport);
        priv->viewport = nullptr;
    }

    if (priv->fractional_scale)
    {
        wp_fractional_scale_v1_destroy(priv->fractional_scale);
        priv->fractional_scale = nullptr;
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

const std::vector<wf::test::touch_event_t>& wf::test::wayland_xdg_client_t::touch_events() const
{
    return priv->touch_events;
}

void wf::test::wayland_xdg_client_t::clear_touch_events()
{
    priv->touch_events.clear();
}
