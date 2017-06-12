#include "panel.hpp"
#include "background.hpp"
#include "gamma.hpp"
#include "../shared/config.hpp"
#include <vector>
#include <map>

wayfire_display display;

struct wayfire_shell_output {
    wayfire_panel* panel;
    wayfire_background *background;
    gamma_adjust *gamma;
};

std::map<uint32_t, wayfire_shell_output> outputs;

std::string bg_path;
void output_created_cb(void *data, wayfire_shell *wayfire_shell,
        uint32_t output, uint32_t width, uint32_t height)
{
    if (bg_path != "none") {
        auto bg = (outputs[output].background = new wayfire_background(bg_path));
        bg->create_background(output, width, height);
    }

    auto panel = (outputs[output].panel = new wayfire_panel());
    panel->create_panel(output, width, height);
}

struct {
    bool enabled;
    int day_start, day_end;
    int day_t, night_t;
} gamma_ops;

void output_gamma_size_cb(void *data, wayfire_shell *shell, uint32_t output,
        uint32_t size)
{
    if (size > 0 && gamma_ops.enabled) {
        outputs[output].gamma = new gamma_adjust(output, size, gamma_ops.day_t,
                gamma_ops.night_t, gamma_ops.day_start, gamma_ops.day_end);
    }
}

static const struct wayfire_shell_listener bg_shell_listener = {
    .output_created = output_created_cb,
    .gamma_size = output_gamma_size_cb
};

int main()
{
    std::string home_dir = secure_getenv("HOME");
    auto config = new wayfire_config(home_dir + "/.config/wayfire.ini");
    auto section = config->get_section("shell");

    bg_path = section->get_string("background", "none");

    gamma_ops.enabled = section->get_int("color_temp_enabled", 0);
    if (gamma_ops.enabled) {
        std::string s = section->get_string("day_start", "08:00");
        int h, m;
        sscanf(s.c_str(), "%d:%d", &h, &m);

        gamma_ops.day_start = h * 60 + m;

        s = section->get_string("day_end", "20:00");
        sscanf(s.c_str(), "%d:%d", &h, &m);
        gamma_ops.day_end = h * 60 + m;

        gamma_ops.day_t = section->get_int("day_temperature", 6500);
        gamma_ops.night_t = section->get_int("night_temperature", 4500);
    }

    if (!setup_wayland_connection())
        return -1;

    /* TODO: parse background src from command line */
    wayfire_shell_add_listener(display.wfshell, &bg_shell_listener, 0);

    while(true) {
        if (wl_display_dispatch(display.wl_disp) < 0)
            break;
    }

    for (auto x : outputs) {
        if (x.second.panel)
            delete x.second.panel;
        if (x.second.background)
            delete x.second.background;
        if (x.second.gamma)
            delete x.second.gamma;
    }

    wl_display_disconnect(display.wl_disp);
}
