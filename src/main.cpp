#include <cstdlib>
#include <cstring>
#include <iostream>
#include <getopt.h>
#include <signal.h>
#include <fcntl.h>

#include <unistd.h>
#include <wayfire/debug.hpp>
#include "main.hpp"

#include <wayland-server.h>

#include "wayfire/config-backend.hpp"
#include "core/plugin-loader.hpp"
#include "core/core-impl.hpp"

static void print_version()
{
    std::cout << WAYFIRE_VERSION << "-" << wf::version::git_commit <<
        " (" __DATE__ ") branch " << wf::version::git_branch << std::endl;
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

    bool enabled =
        wf::log::enabled_categories.test((int)wf::log::logging_category::WLR);
    if ((wlevel == wf::log::LOG_LEVEL_DEBUG) && !enabled)
    {
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
    std::string config_plugin(backend);

    if (backend.size())
    {
        std::vector<std::string> plugin_prefixes = wf::get_plugin_paths();
        config_plugin =
            wf::get_plugin_path_for_name(plugin_prefixes, backend).value_or("");
    }

    auto [_, init_ptr] = wf::get_new_instance_handle(config_plugin);

    if (!init_ptr)
    {
        return nullptr;
    }

    using backend_init_t = wf::config_backend_t * (*)();
    auto init = wf::union_cast<void*, backend_init_t>(init_ptr);
    return init();
}

void parse_extended_debugging(const std::vector<std::string>& categories)
{
    for (const auto& cat : categories)
    {
        if (cat == "txn")
        {
            LOGD("Enabling extended debugging for transactions");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::TXN, 1);
        } else if (cat == "txni")
        {
            LOGD("Enabling extended debugging for transaction objects");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::TXNI, 1);
        } else if (cat == "views")
        {
            LOGD("Enabling extended debugging for views");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::VIEWS, 1);
        } else if (cat == "wlroots")
        {
            LOGD("Enabling extended debugging for wlroots");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::WLR, 1);
        } else if (cat == "scanout")
        {
            LOGD("Enabling extended debugging for direct scanout");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::SCANOUT, 1);
        } else if (cat == "pointer")
        {
            LOGD("Enabling extended debugging for pointer events");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::POINTER, 1);
        } else if (cat == "wset")
        {
            LOGD("Enabling extended debugging for workspace set events");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::WSET, 1);
        } else if (cat == "kbd")
        {
            LOGD("Enabling extended debugging for keyboard events");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::KBD, 1);
        } else if (cat == "xwayland")
        {
            LOGD("Enabling extended debugging for xwayland events");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::XWL, 1);
        } else if (cat == "layer-shell")
        {
            LOGD("Enabling extended debugging for layer-shell events");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::LSHELL, 1);
        } else if (cat == "im")
        {
            LOGD("Enabling extended debugging for input method events");
            wf::log::enabled_categories.set((size_t)wf::log::logging_category::IM, 1);
        } else
        {
            LOGE("Unrecognized debugging category \"", cat, "\"");
        }
    }
}

// extern "C" {
// void __cxa_throw(void *ex, void *info, void (*dest)(void*))
// {
// wf::print_trace(false);
// std::_Exit(-1);
// }
// }
//
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
        {"debug", optional_argument, NULL, 'd'},
        {"damage-debug", no_argument, NULL, 'D'},
        {"damage-rerender", no_argument, NULL, 'R'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {0, 0, NULL, 0}
    };

    std::string config_file;
    std::string config_backend = WF_DEFAULT_CONFIG_BACKEND;
    std::vector<std::string> extended_debug_categories;

    int c, i;
    while ((c = getopt_long(argc, argv, "c:B:d::DhRv", opts, &i)) != -1)
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

            // Make sure to parse things like `-d txn`, which getopt does not.
            // According to documentation, for optional arguments, optarg will
            // be NULL so we need to manually check.
            if (!optarg && (NULL != argv[optind]) &&
                ('-' != argv[optind][0]))
            {
                optarg = argv[optind];
                ++optind;
            }

            if (optarg)
            {
                extended_debug_categories.push_back(optarg);
            }

            break;

          case 'v':
            print_version();
            break;

          default:
            std::cerr << "Unrecognized command line argument " << optarg << "\n" <<
                std::endl;
        }
    }

    /* Don't crash on SIGPIPE, e.g., when doing IPC to a client whose fd has been closed. */
    signal(SIGPIPE, SIG_IGN);

    wf::log::initialize_logging(std::cout, log_level, detect_color_mode());

    parse_extended_debugging(extended_debug_categories);
    wlr_log_init(WLR_DEBUG, wlr_log_handler);

#ifdef PRINT_TRACE
    /* In case of crash, print the stacktrace for debugging.
     * However, if ASAN is enabled, we'll get better stacktrace from there. */
    signal(SIGSEGV, signal_handler);
    signal(SIGFPE, signal_handler);
    signal(SIGABRT, signal_handler);
#endif

    std::set_terminate([] ()
    {
        std::cout << "Unhandled exception" << std::endl;
        wf::print_trace(false);
        std::abort();
    });

    LOGI("Starting wayfire version ", WAYFIRE_VERSION);
    /* First create display and initialize safe-list's event loop, so that
     * wf objects (which depend on safe-list) can work */
    auto display = wl_display_create();
    auto& core   = wf::get_core_impl();

    core.argc = argc;
    core.argv = argv;

    /** TODO: move this to core_impl constructor */
    core.display = display;
    core.ev_loop = wl_display_get_event_loop(core.display);
    core.backend = wlr_backend_autocreate(core.display, &core.session);

    int drm_fd = wlr_backend_get_drm_fd(core.backend);
    if (drm_fd < 0)
    {
        char *drm_device = getenv("WLR_RENDER_DRM_DEVICE");
        if (drm_device)
        {
            drm_fd = open(drm_device, O_RDWR | O_CLOEXEC);
        }

        if (drm_fd < 0)
        {
            LOGE("Failed to get DRM file descriptor,",
                " try specifying a valid WLR_RENDER_DRM_DEVICE!");
            wl_display_destroy_clients(core.display);
            wl_display_destroy(core.display);
            return EXIT_FAILURE;
        }
    }

    core.renderer = wlr_gles2_renderer_create_with_drm_fd(drm_fd);
    assert(core.renderer);
    core.allocator = wlr_allocator_autocreate(core.backend, core.renderer);
    assert(core.allocator);
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
