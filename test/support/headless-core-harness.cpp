#include "headless-core-harness.hpp"

#include <config.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <array>
#include <vector>

#include <drm_fourcc.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wayfire/config-backend.hpp>
#include <wayfire/config/file.hpp>
#include <wayfire/core.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util/log.hpp>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "../../src/core/core-impl.hpp"
#include "../../src/core/seat/seat-impl.hpp"
#include "../../src/core/seat/touch.hpp"

namespace
{
class test_config_backend_t : public wf::config_backend_t
{
    std::string extra_config;

  public:
    explicit test_config_backend_t(std::string extra_config = {}) :
        extra_config(std::move(extra_config))
    {}

    void init(wl_display*, wf::config::config_manager_t& config,
        const std::string&) override
    {
        setenv("WAYFIRE_PLUGIN_XML_PATH", TEST_METADATA_DIR, 1);
        config = wf::config::build_configuration(get_xml_dirs(), "/dev/null",
            "/dev/null");

        wf::config::load_configuration_options_from_string(config,
            "[core]\n"
            "background_color = 0.0 0.0 0.0 1.0\n"
            "plugins = \n"
            "xwayland = false\n"
            "\n"
            "[autostart]\n"
            "autostart_wf_shell = false\n"
            "\n"
            "[workarounds]\n"
            "auto_reload_config = false\n"
            "enable_input_method_v2 = false\n"
            "use_external_output_configuration = false\n",
            "default-test-config");

        if (!extra_config.empty())
        {
            wf::config::load_configuration_options_from_string(config,
                extra_config, "headless-core-harness-extra-config");
        }
    }
};

static std::string add_test_socket(wl_display *display)
{
    const char *socket = wl_display_add_socket_auto(display);
    if (!socket)
    {
        throw std::runtime_error("Failed to create Wayland socket for test harness");
    }

    return socket;
}

static bool is_usable_runtime_dir(const char *path)
{
    if (!path || !path[0])
    {
        return false;
    }

    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode) && (access(path, W_OK | X_OK) == 0);
}

static std::string ensure_test_runtime_dir()
{
    if (is_usable_runtime_dir(getenv("XDG_RUNTIME_DIR")))
    {
        return {};
    }

    auto base = std::filesystem::temp_directory_path() / "wayfire-test-runtime-XXXXXX";
    std::array<char, PATH_MAX> runtime_dir{};
    auto path = base.string();
    if (path.size() >= runtime_dir.size())
    {
        throw std::runtime_error("Temporary runtime directory path is too long");
    }

    std::snprintf(runtime_dir.data(), runtime_dir.size(), "%s", path.c_str());
    if (!mkdtemp(runtime_dir.data()))
    {
        throw std::runtime_error("Failed to create temporary runtime directory for test harness");
    }

    std::filesystem::permissions(runtime_dir.data(),
        std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace);
    setenv("XDG_RUNTIME_DIR", runtime_dir.data(), 1);
    return runtime_dir.data();
}
}

struct wf::test::headless_core_harness_t::impl
{
    wf::compositor_core_impl_t *core = nullptr;
    std::string old_wayland_display;
    std::string old_xdg_runtime_dir;
    std::string test_runtime_dir;
    bool had_wayland_display = false;
    bool had_xdg_runtime_dir = false;
    uint32_t touch_time = 1000;

    std::vector<uint32_t> capture_output_pixels()
    {
        auto *wo = core->output_layout->get_outputs().front();
        std::optional<std::vector<uint32_t>> captured_pixels;
        wf::post_hook_t capture_hook = [&] (wf::auxilliary_buffer_t& source, const wf::render_buffer_t&)
        {
            auto framebuffer = source.get_renderbuffer();
            auto *buffer     = framebuffer.get_buffer();
            if (!buffer)
            {
                return;
            }

            auto *tex = wlr_texture_from_buffer(core->renderer, buffer);
            if (!tex)
            {
                throw std::runtime_error("Failed to create texture from output buffer");
            }

            auto size = framebuffer.get_size();
            std::vector<uint32_t> pixels(size.width * size.height);
            wlr_texture_read_pixels_options opts{};
            opts.data   = pixels.data();
            opts.format = DRM_FORMAT_RGBA8888;
            opts.stride = size.width * 4;
            if (!wlr_texture_read_pixels(tex, &opts))
            {
                wlr_texture_destroy(tex);
                throw std::runtime_error("Failed to read output pixels");
            }

            wlr_texture_destroy(tex);
            captured_pixels = std::move(pixels);
        };

        wo->render->add_post(&capture_hook);
        wo->render->damage_whole();
        wo->render->schedule_redraw();
        for (int i = 0; i < 50 && !captured_pixels.has_value(); ++i)
        {
            wl_event_loop_dispatch(core->ev_loop, 10);
            wl_display_flush_clients(core->display);
        }

        wo->render->rem_post(&capture_hook);
        if (!captured_pixels.has_value())
        {
            throw std::runtime_error("No output buffer available for capture");
        }

        return *captured_pixels;
    }
};

wf::test::headless_core_harness_t::headless_core_harness_t(std::string extra_config, bool start_plugins)
{
    wf::log::initialize_logging(std::cout, wf::log::LOG_LEVEL_DEBUG,
        wf::log::LOG_COLOR_MODE_OFF);
    wlr_log_init(WLR_ERROR, nullptr);

    priv = std::make_unique<impl>();
    priv->core = &wf::compositor_core_impl_t::allocate_core();

    if (const char *old = getenv("WAYLAND_DISPLAY"))
    {
        priv->had_wayland_display = true;
        priv->old_wayland_display = old;
    }

    if (const char *old = getenv("XDG_RUNTIME_DIR"))
    {
        priv->had_xdg_runtime_dir = true;
        priv->old_xdg_runtime_dir = old;
    }

    priv->test_runtime_dir = ensure_test_runtime_dir();

    auto& core = *priv->core;
    core.display = wl_display_create();
    core.session = nullptr;
    if (!core.display)
    {
        throw std::runtime_error("Failed to create Wayland display");
    }

    core.ev_loop = wl_display_get_event_loop(core.display);
    core.wayland_display = add_test_socket(core.display);
    setenv("WAYLAND_DISPLAY", core.wayland_display.c_str(), 1);

    core.backend = wlr_headless_backend_create(core.ev_loop);
    if (!core.backend)
    {
        throw std::runtime_error("Failed to create headless backend");
    }

    core.renderer = wlr_pixman_renderer_create();
    if (!core.renderer)
    {
        throw std::runtime_error("Failed to create renderer");
    }

    core.allocator = wlr_allocator_autocreate(core.backend, core.renderer);
    if (!core.allocator)
    {
        throw std::runtime_error("Failed to create allocator");
    }

    core.config_backend = std::make_unique<test_config_backend_t>(std::move(extra_config));
    core.config_backend->init(core.display, *core.config, "");
    core.init();

    if (!wlr_backend_start(core.backend))
    {
        throw std::runtime_error("Failed to start headless backend");
    }

    auto *output_handle = wlr_headless_add_output(core.backend, 1280, 720);
    if (!output_handle)
    {
        throw std::runtime_error("Failed to create headless output");
    }

    roundtrip();
    auto outputs = core.output_layout->get_outputs();
    if (!outputs.empty())
    {
        outputs.front()->ensure_pointer(true);
        core.seat->focus_output(outputs.front());
    }

    if (start_plugins)
    {
        core.post_init();
    }

    roundtrip();
}

wf::test::headless_core_harness_t::~headless_core_harness_t()
{
    if (priv && priv->core)
    {
        wf::compositor_core_impl_t::deallocate_core();
    }

    if (priv && priv->had_wayland_display)
    {
        setenv("WAYLAND_DISPLAY", priv->old_wayland_display.c_str(), 1);
    } else
    {
        unsetenv("WAYLAND_DISPLAY");
    }

    if (priv && priv->had_xdg_runtime_dir)
    {
        setenv("XDG_RUNTIME_DIR", priv->old_xdg_runtime_dir.c_str(), 1);
    } else
    {
        unsetenv("XDG_RUNTIME_DIR");
    }

    if (priv && !priv->test_runtime_dir.empty())
    {
        std::error_code ec;
        std::filesystem::remove_all(priv->test_runtime_dir, ec);
    }
}

void wf::test::headless_core_harness_t::dispatch_once(int timeout_ms)
{
    wl_event_loop_dispatch(priv->core->ev_loop, timeout_ms);
    wl_display_flush_clients(priv->core->display);
}

void wf::test::headless_core_harness_t::roundtrip()
{
    dispatch_once(0);
    dispatch_once(0);
}

bool wf::test::headless_core_harness_t::run_until(const std::function<bool()>& predicate,
    int max_iterations)
{
    for (int i = 0; i < max_iterations; ++i)
    {
        if (predicate())
        {
            return true;
        }

        dispatch_once(10);
    }

    return predicate();
}

void wf::test::headless_core_harness_t::enable_touch_input()
{
    auto *seat = priv->core->seat->seat;
    wlr_seat_set_capabilities(seat,
        seat->capabilities | WL_SEAT_CAPABILITY_TOUCH);
}

void wf::test::headless_core_harness_t::touch_down(int32_t id, double x, double y)
{
    priv->core->seat->priv->touch->handle_touch_down(id, priv->touch_time++,
        {x, y}, wf::input_event_processing_mode_t::FULL);
}

void wf::test::headless_core_harness_t::touch_motion(int32_t id, double x, double y)
{
    priv->core->seat->priv->touch->handle_touch_motion(id, priv->touch_time++,
        {x, y}, true, wf::input_event_processing_mode_t::FULL);
}

void wf::test::headless_core_harness_t::touch_up(int32_t id)
{
    priv->core->seat->priv->touch->handle_touch_up(id, priv->touch_time++,
        wf::input_event_processing_mode_t::FULL);
}

void wf::test::headless_core_harness_t::touch_frame()
{
    wlr_seat_touch_notify_frame(priv->core->seat->seat);
    wl_display_flush_clients(priv->core->display);
}

wf::output_t*wf::test::headless_core_harness_t::output() const
{
    auto outputs = priv->core->output_layout->get_outputs();
    return outputs.empty() ? nullptr : outputs.front();
}

const std::string& wf::test::headless_core_harness_t::socket_name() const
{
    return priv->core->wayland_display;
}

std::vector<uint32_t> wf::test::headless_core_harness_t::capture_output_pixels()
{
    return priv->capture_output_pixels();
}
