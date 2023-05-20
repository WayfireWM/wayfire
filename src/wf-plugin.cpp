#include <cstdlib>
#include <cstring>
#include <iostream>
#include <getopt.h>
#include <signal.h>
#include <map>
#include <fcntl.h>
#include <filesystem>

#include <unistd.h>
#include <wayfire/debug.hpp>
#include "main.hpp"
#include "wayfire/nonstd/safe-list.hpp"

#include <wayland-server.h>

#include "wayfire/config-backend.hpp"
#include "core/plugin-loader.hpp"
#include "core/core-impl.hpp"
#include "wayfire/output.hpp"

#include <sys/wait.h>

class plugin
{
  public:
    const std::string separator = "&";
    const std::string wf_url;
    std::string url  = "";
    std::string name = "";
    bool precompiled = false;
    std::string xdg_cache_dir;
    std::string xdg_config_dir;
    std::string xdg_data_dir;
    std::string cache_dir;
    std::string config_dir;
    std::string data_dir;
    std::string downloaded_file;

    plugin(std::string wf_url) : wf_url(wf_url)
    {
        parse();
        xdg_dirs();
    }

    void xdg_dirs()
    {
        char *c_xdg_cache_dir  = std::getenv("XDG_CACHE_HOME");
        char *c_xdg_config_dir = std::getenv("XDG_CONFIG_HOME");
        char *c_xdg_data_dir   = std::getenv("XDG_DATA_HOME");

        if (c_xdg_cache_dir != NULL)
        {
            xdg_cache_dir = c_xdg_cache_dir;
        } else
        {
            xdg_cache_dir = (std::string)std::getenv("HOME") + "/.cache";
        }

        if (c_xdg_config_dir != NULL)
        {
            xdg_config_dir = c_xdg_config_dir;
        } else
        {
            xdg_config_dir = (std::string)std::getenv("HOME") + "/.config";
        }

        if (c_xdg_data_dir != NULL)
        {
            xdg_data_dir = c_xdg_data_dir;
        } else
        {
            xdg_data_dir = (std::string)std::getenv("HOME") + "/.local/share";
        }

        if ((xdg_data_dir == "") || (xdg_config_dir == "") || (xdg_cache_dir == ""))
        {
            throw std::invalid_argument("Invalid XDG directories");
        }

        std::string xdg_dirs[3] = {xdg_cache_dir, xdg_config_dir, xdg_data_dir};
        std::string sub_dirs[4] = {"/wayfire", "/wayfire/plugins", "/wayfire/plugins/" + name};
        for (std::string dir : xdg_dirs)
        {
            for (std::string sub_dir : sub_dirs)
            {
                if (!std::filesystem::exists(dir + sub_dir))
                {
                    std::filesystem::create_directories(dir + sub_dir);
                }
            }
        }

        cache_dir  = xdg_cache_dir + "/wayfire/plugins/" + name;
        config_dir = xdg_config_dir + "/wayfire/plugins/" + name;
        data_dir   = xdg_data_dir + "/wayfire";
    }

    void parse()
    {
        std::string params = wf_url.substr(wf_url.find("?") + 1, wf_url.length());
        if (params[params.length() - 1] != '&')
        {
            params += "&";
        }

        size_t pos = 0;
        std::string token;
        while ((pos = params.find(separator)) != std::string::npos)
        {
            token = params.substr(0, pos);
            size_t pos2     = token.find("=");
            std::string key = token.substr(0, pos2);
            std::string value = token.substr(pos2 + 1, token.length());
            params.erase(0, pos + separator.length());
            if (key == "url")
            {
                url = value;
            } else if (key == "name")
            {
                name = value;
            } else if (key == "precompiled")
            {
                precompiled = value == "true";
            }
        }

        if ((url == "") || (name == ""))
        {
            throw std::invalid_argument("Invalid plugin url or name");
        }
    }

    void download()
    {
        std::string output = cache_dir + "/" + name + ".tar.gz";
        int downloaded     = std::system(("curl --location " + url + " --output " + output).c_str());
        if (downloaded != 0)
        {
            throw std::runtime_error("Failed to download plugin");
        }

        downloaded_file = cache_dir + "/" + name + ".tar.gz";
    }

    void install()
    {
        int installed = std::system(("tar xzvf " + downloaded_file + " --directory " + data_dir).c_str());
        if (installed != 0)
        {
            throw std::runtime_error("Failed to install plugin");
        }
    }
};

int main(int argc, char *argv[])
{
    plugin p(argv[1]);
    p.download();
    p.install();
}
