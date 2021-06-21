#include <cstdlib>
#include <cstring>
#include <iostream>
#include <getopt.h>
#include <signal.h>
#include <map>

#include <unistd.h>
#include "debug-func.hpp"
#include "main.hpp"
#include "wayfire/nonstd/safe-list.hpp"

#include <wayland-server.h>

#include "wayfire/config-backend.hpp"
#include "output/plugin-loader.hpp"
#include "core/core-impl.hpp"
#include "wayfire/output.hpp"

static void print_version()
{
    std::cout << WAYFIRE_VERSION << std::endl;
    exit(0);
}

static void print_help()
{
    std::cout << "Wayfire " << WAYFIRE_VERSION << std::endl;
    std::cout << "Usage: wayfire [OPTION]...\n" << std::endl;
    std::cout << " -c,  --config            specify config file to use " <<
        "(overrides WAYFIRE_CONFIG_FILE from the environment)" << std::endl;
    std::cout << " -B,  --config-backend    specify config backend to use" <<
        std::endl;
    std::cout << " -h,  --help              print this help" << std::endl;
    std::cout << " -d,  --debug             enable debug logging" << std::endl;
    std::cout <<
        " -D,  --damage-debug      enable additional debug for damaged regions" <<
        std::endl;
    std::cout << " -R,  --damage-rerender   rerender damaged regions" << std::endl;
    std::cout << " -v,  --version           print version and exit" << std::endl;
    exit(0);
}

static bool drop_permissions(void)
{
    if ((getuid() != geteuid()) || (getgid() != getegid()))
    {
        // Set the gid and uid in the correct order.
        if ((setgid(getgid()) != 0) || (setuid(getuid()) != 0))
        {
            LOGE("Unable to drop root, refusing to start");

            return false;
        }
    }

    if ((setgid(0) != -1) || (setuid(0) != -1))
    {
        LOGE("Unable to drop root (we shouldn't be able to "
             "restore it after setuid), refusing to start");

        return false;
    }

    return true;
}

static wf::log::color_mode_t detect_color_mode()
{
    return isatty(STDOUT_FILENO) ?
           wf::log::LOG_COLOR_MODE_ON : wf::log::LOG_COLOR_MODE_OFF;
}

static void wlr_log_handler(wlr_log_importance level,
    const char *fmt, va_list args)
{
    const int bufsize = 4 * 1024;
    char buffer[bufsize];
    vsnprintf(buffer, bufsize, fmt, args);

    wf::log::log_level_t wlevel;
    switch (level)
    {
      case WLR_ERROR:
        wlevel = wf::log::LOG_LEVEL_ERROR;
        break;

      case WLR_INFO:
        wlevel = wf::log::LOG_LEVEL_INFO;
        break;

      case WLR_DEBUG:
        wlevel = wf::log::LOG_LEVEL_DEBUG;
        break;

      default:
        return;
    }

    wf::log::log_plain(wlevel, buffer);
}

#ifdef PRINT_TRACE
static void signal_handler(int signal)
{
    std::string error;
    switch (signal)
    {
      case SIGSEGV:
        error = "Segmentation fault";
        break;

      case SIGFPE:
        error = "Floating-point exception";
        break;

      case SIGABRT:
        error = "Fatal error(SIGABRT)";
        break;

      default:
        error = "Unknown";
    }

    LOGE("Fatal error: ", error);
    wf::print_trace(false);
    std::_Exit(-1);
}

#endif

static std::optional<std::string> choose_socket(wl_display *display)
{
    for (int i = 1; i <= 32; i++)
    {
        auto name = "wayland-" + std::to_string(i);
        if (wl_display_add_socket(display, name.c_str()) >= 0)
        {
            return name;
        }
    }

    return {};
}

static wf::config_backend_t *load_backend(const std::string& backend)
{
    auto [_, init_ptr] = wf::get_new_instance_handle(backend);

    if (!init_ptr)
    {
        return nullptr;
    }

    using backend_init_t = wf::config_backend_t * (*)();
    auto init = wf::union_cast<void*, backend_init_t>(init_ptr);
    return init();
}

int main(int argc, char *argv[])
{
    wf::log::log_level_t log_level = wf::log::LOG_LEVEL_INFO;
    struct option opts[] = {
        {
            "config", required_argument, NULL, 'c'
        },
        {
            "config-backend", required_argument, NULL, 'B'
        },
        {"debug", no_argument, NULL, 'd'},
        {"damage-debug", no_argument, NULL, 'D'},
        {"damage-rerender", no_argument, NULL, 'R'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {0, 0, NULL, 0}
    };

    std::string config_file;
    std::string config_backend = WF_DEFAULT_CONFIG_BACKEND;

    int c, i;
    while ((c = getopt_long(argc, argv, "c:B:dDhRv", opts, &i)) != -1)
    {
        switch (c)
        {
          case 'c':
            config_file = optarg;
            break;

          case 'B':
            config_backend = optarg;
            break;

          case 'D':
            runtime_config.damage_debug = true;
            break;

          case 'R':
            runtime_config.no_damage_track = true;
            break;

          case 'h':
            print_help();
            break;

          case 'd':
            log_level = wf::log::LOG_LEVEL_DEBUG;
            break;

          case 'v':
            print_version();
            break;

          default:
            std::cerr << "Unrecognized command line argument " << optarg << "\n" <<
                std::endl;
        }
    }

    auto wlr_log_level =
        (log_level == wf::log::LOG_LEVEL_DEBUG ? WLR_DEBUG : WLR_ERROR);
    wlr_log_init(wlr_log_level, wlr_log_handler);
    wf::log::initialize_logging(std::cout, log_level, detect_color_mode());

#ifdef PRINT_TRACE
    /* In case of crash, print the stacktrace for debugging.
     * However, if ASAN is enabled, we'll get better stacktrace from there. */
    signal(SIGSEGV, signal_handler);
    signal(SIGFPE, signal_handler);
    signal(SIGABRT, signal_handler);
#endif

    LOGI("Starting wayfire version ", WAYFIRE_VERSION);
    /* First create display and initialize safe-list's event loop, so that
     * wf objects (which depend on safe-list) can work */
    auto display = wl_display_create();
    auto& core   = wf::get_core_impl();

    core.argc = argc;
    core.argv = argv;

    /** TODO: move this to core_impl constructor */
    core.display  = display;
    core.ev_loop  = wl_display_get_event_loop(core.display);
    core.backend  = wlr_backend_autocreate(core.display);
    core.renderer = wlr_backend_get_renderer(core.backend);
    core.egl = wlr_gles2_renderer_get_egl(core.renderer);
    assert(core.egl);

    if (!drop_permissions())
    {
        wl_display_destroy_clients(core.display);
        wl_display_destroy(core.display);

        return EXIT_FAILURE;
    }

    auto backend = load_backend(config_backend);
    if (!backend)
    {
        LOGE("Failed to load configuration backend!");
        wl_display_destroy_clients(core.display);
        wl_display_destroy(core.display);
        return EXIT_FAILURE;
    }

    LOGD("Using configuration backend: ", config_backend);
    core.config_backend = std::unique_ptr<wf::config_backend_t>(backend);
    core.config_backend->init(display, core.config, config_file);
    core.init();

    auto socket = choose_socket(core.display);
    if (!socket)
    {
        LOGE("Failed to create wayland socket, exiting.");

        return -1;
    }

    core.wayland_display = socket.value();
    LOGI("Using socket name ", core.wayland_display);
    if (!wlr_backend_start(core.backend))
    {
        LOGE("Failed to initialize backend, exiting");
        wlr_backend_destroy(core.backend);
        wl_display_destroy(core.display);

        return -1;
    }

    setenv("WAYLAND_DISPLAY", core.wayland_display.c_str(), 1);
    core.post_init();

    wl_display_run(core.display);

    /* Teardown */
    wl_display_destroy_clients(core.display);
    wl_display_destroy(core.display);
    return EXIT_SUCCESS;
}
