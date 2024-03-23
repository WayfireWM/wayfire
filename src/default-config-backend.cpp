#include <vector>
#include "wayfire/debug.hpp"
#include "wayfire/signal-definitions.hpp"
#include <string>
#include <wayfire/config/file.hpp>
#include <wayfire/config-backend.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>

#include <cstring>
#include <sys/inotify.h>
#include <filesystem>
#include <unistd.h>

#define INOT_BUF_SIZE (sizeof(inotify_event) + NAME_MAX + 1)

static std::string config_dir, config_file;
wf::config::config_manager_t *cfg_manager;

static int wd_cfg_dir, wd_cfg_file;

static void add_watch(int fd)
{
    wd_cfg_dir  = inotify_add_watch(fd, config_dir.c_str(), IN_CREATE | IN_MOVED_TO);
    wd_cfg_file = inotify_add_watch(fd, config_file.c_str(), IN_CLOSE_WRITE);
}

static void reload_config(int fd)
{
    wf::config::load_configuration_options_from_file(*cfg_manager, config_file);
}

static int handle_config_updated(int fd, uint32_t mask, void *data)
{
    if ((mask & WL_EVENT_READABLE) == 0)
    {
        return 0;
    }

    char buf[INOT_BUF_SIZE] __attribute__((aligned(alignof(inotify_event))));

    bool should_reload = false;
    inotify_event *event;

    // Reading from the inotify FD is guaranteed to not read partial events.
    // From inotify(7):
    // Each successful read(2) returns a buffer containing
    // one or more [..] structures
    auto len = read(fd, buf, INOT_BUF_SIZE);
    if (len < 0)
    {
        return 0;
    }

    const auto cfg_file_basename = std::filesystem::path(config_file).filename().string();

    for (char *ptr = buf;
         ptr < (buf + len);
         ptr += sizeof(inotify_event) + event->len)
    {
        event = reinterpret_cast<inotify_event*>(ptr);
        // We reload in two cases:
        //
        // 1. The config file itself was modified, or...
        should_reload |= event->wd == wd_cfg_file;
        // 2. The config file was moved nto or created inside the parent directory.
        if (event->len > 0)
        {
            // This is UB unless event->len > 0.
            auto name_matches = cfg_file_basename == event->name;

            if (name_matches)
            {
                inotify_rm_watch(fd, wd_cfg_file);
                wd_cfg_file =
                    inotify_add_watch(fd, (config_dir + "/" + cfg_file_basename).c_str(), IN_CLOSE_WRITE);
            }

            should_reload |= name_matches;
        }
    }

    if (should_reload)
    {
        LOGD("Reloading configuration file");

        reload_config(fd);
        wf::reload_config_signal ev;
        wf::get_core().emit(&ev);
    }

    return 0;
}

static const char *CONFIG_FILE_ENV = "WAYFIRE_CONFIG_FILE";

namespace wf
{
class dynamic_ini_config_t : public wf::config_backend_t
{
  public:
    void init(wl_display *display, config::config_manager_t& config,
        const std::string& cfg_file) override
    {
        cfg_manager = &config;

        config_file = choose_cfg_file(cfg_file);
        std::filesystem::path path = std::filesystem::absolute(config_file);
        config_dir = path.parent_path();
        LOGI("Using config file: ", config_file.c_str());
        setenv(CONFIG_FILE_ENV, config_file.c_str(), 1);

        config = wf::config::build_configuration(
            get_xml_dirs(), SYSCONFDIR "/wayfire/defaults.ini", config_file);

        int inotify_fd = inotify_init1(IN_CLOEXEC);
        reload_config(inotify_fd);
        add_watch(inotify_fd);

        wl_event_loop_add_fd(wl_display_get_event_loop(display),
            inotify_fd, WL_EVENT_READABLE, handle_config_updated, NULL);
    }

    std::string choose_cfg_file(const std::string& cmdline_cfg_file)
    {
        std::string env_cfg_file = nonull(getenv(CONFIG_FILE_ENV));
        if (!cmdline_cfg_file.empty())
        {
            if ((env_cfg_file != nonull(NULL)) &&
                (cmdline_cfg_file != env_cfg_file))
            {
                LOGW("Wayfire config file specified in the environment is ",
                    "overridden by the command line arguments!");
            }

            return cmdline_cfg_file;
        }

        if (env_cfg_file != nonull(NULL))
        {
            return env_cfg_file;
        }

        return (getenv("XDG_CONFIG_HOME") ?:
            (std::string(nonull(getenv("HOME"))) + "/.config")) +
               "/wayfire.ini";
    }
};
}

DECLARE_WAYFIRE_CONFIG_BACKEND(wf::dynamic_ini_config_t);
