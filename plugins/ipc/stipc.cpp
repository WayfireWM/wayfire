#include "wayfire/plugin.hpp"
#include "wayfire/util.hpp"
#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/output-layout.hpp>
#include <getopt.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#define WAYFIRE_PLUGIN
#include <wayfire/debug.hpp>

#include "ipc.hpp"
#include <wayfire/touch/touch.hpp>

extern "C" {
#include <wlr/backend/wayland.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/headless.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/interfaces/wlr_tablet_tool.h>
#include <wlr/interfaces/wlr_tablet_pad.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <libevdev/libevdev.h>
}

#include <wayfire/util/log.hpp>
#include <wayfire/core.hpp>

#define EXPECT_FIELD(data, field, type) \
    if (!data.count(field)) \
    { \
        return get_error("Missing \"" field "\""); \
    } \
    else if (!data[field].is_ ## type()) \
    { \
        return get_error("Field \"" field "\" does not have the correct type " #type); \
    }

static void locate_wayland_backend(wlr_backend *backend, void *data)
{
    if (wlr_backend_is_wl(backend))
    {
        wlr_backend **result = (wlr_backend**)data;
        *result = backend;
    }
}

namespace wf
{
static nlohmann::json geometry_to_json(wf::geometry_t g)
{
    nlohmann::json j;
    j["x"]     = g.x;
    j["y"]     = g.y;
    j["width"] = g.width;
    j["height"] = g.height;
    return j;
}

static std::string layer_to_string(uint32_t layer)
{
    switch (layer)
    {
      case LAYER_BACKGROUND:
        return "background";

      case LAYER_BOTTOM:
        return "bottom";

      case LAYER_WORKSPACE:
        return "workspace";

      case LAYER_TOP:
        return "top";

      case LAYER_UNMANAGED:
        return "unmanaged";

      case LAYER_LOCK:
        return "lock";

      case LAYER_DESKTOP_WIDGET:
        return "dew";

      default:
        break;
    }

    return "none";
}

static const struct wlr_pointer_impl pointer_impl = {
    .name = "stipc-pointer",
};

static void led_update(wlr_keyboard *keyboard, uint32_t leds)
{}

static const struct wlr_keyboard_impl keyboard_impl = {
    .name = "stipc-keyboard",
    .led_update = led_update,
};

static const struct wlr_touch_impl touch_impl = {
    .name = "stipc-touch-device",
};

static const struct wlr_tablet_impl tablet_impl = {
    .name = "stipc-tablet",
};

static const struct wlr_tablet_pad_impl tablet_pad_impl = {
    .name = "stipc-tablet-pad",
};

static void init_wlr_tool(wlr_tablet_tool *tablet_tool)
{
    std::memset(tablet_tool, 0, sizeof(*tablet_tool));
    tablet_tool->type     = WLR_TABLET_TOOL_TYPE_PEN;
    tablet_tool->pressure = true;
    wl_signal_init(&tablet_tool->events.destroy);
}

class headless_input_backend_t
{
  public:
    wlr_backend *backend;
    wlr_pointer pointer;
    wlr_keyboard keyboard;
    wlr_touch touch;
    wlr_tablet tablet;
    wlr_tablet_tool tablet_tool;
    wlr_tablet_pad tablet_pad;

    headless_input_backend_t()
    {
        auto& core = wf::get_core();
        backend = wlr_headless_backend_create(core.display);
        wlr_multi_backend_add(core.backend, backend);

        wlr_pointer_init(&pointer, &pointer_impl, "stipc_pointer");
        wlr_keyboard_init(&keyboard, &keyboard_impl, "stipc_keyboard");
        wlr_touch_init(&touch, &touch_impl, "stipc_touch");
        wlr_tablet_init(&tablet, &tablet_impl, "stipc_tablet_tool");
        wlr_tablet_pad_init(&tablet_pad, &tablet_pad_impl, "stipc_tablet_pad");
        init_wlr_tool(&tablet_tool);

        wl_signal_emit_mutable(&backend->events.new_input, &pointer.base);
        wl_signal_emit_mutable(&backend->events.new_input, &keyboard.base);
        wl_signal_emit_mutable(&backend->events.new_input, &touch.base);
        wl_signal_emit_mutable(&backend->events.new_input, &tablet.base);
        wl_signal_emit_mutable(&backend->events.new_input, &tablet_pad.base);

        if (core.get_current_state() == compositor_state_t::RUNNING)
        {
            wlr_backend_start(backend);
        }

        wl_signal_emit_mutable(&tablet_pad.events.attach_tablet, &tablet_tool);
    }

    ~headless_input_backend_t()
    {
        auto& core = wf::get_core();
        wlr_pointer_finish(&pointer);
        wlr_keyboard_finish(&keyboard);
        wlr_touch_finish(&touch);
        wlr_multi_backend_remove(core.backend, backend);
        wlr_backend_destroy(backend);
    }

    void do_key(uint32_t key, wl_keyboard_key_state state)
    {
        wlr_keyboard_key_event ev;
        ev.keycode = key;
        ev.state   = state;
        ev.update_state = true;
        ev.time_msec    = get_current_time();
        wlr_keyboard_notify_key(&keyboard, &ev);
    }

    void do_button(uint32_t button, wlr_button_state state)
    {
        wlr_pointer_button_event ev;
        ev.pointer   = &pointer;
        ev.button    = button;
        ev.state     = state;
        ev.time_msec = get_current_time();
        wl_signal_emit(&pointer.events.button, &ev);
        wl_signal_emit(&pointer.events.frame, NULL);
    }

    void do_motion(double x, double y)
    {
        auto cursor = wf::get_core().get_cursor_position();

        wlr_pointer_motion_event ev;
        ev.pointer   = &pointer;
        ev.time_msec = get_current_time();
        ev.delta_x   = ev.unaccel_dx = x - cursor.x;
        ev.delta_y   = ev.unaccel_dy = y - cursor.y;
        wl_signal_emit(&pointer.events.motion, &ev);
        wl_signal_emit(&pointer.events.frame, NULL);
    }

    void convert_xy_to_relative(double *x, double *y)
    {
        auto layout = wf::get_core().output_layout->get_handle();
        wlr_box box;
        wlr_output_layout_get_box(layout, NULL, &box);
        *x = 1.0 * (*x - box.x) / box.width;
        *y = 1.0 * (*y - box.y) / box.height;
    }

    void do_touch(int finger, double x, double y)
    {
        convert_xy_to_relative(&x, &y);
        if (!wf::get_core().get_touch_state().fingers.count(finger))
        {
            wlr_touch_down_event ev;
            ev.touch     = &touch;
            ev.time_msec = get_current_time();
            ev.x = x;
            ev.y = y;
            ev.touch_id = finger;
            wl_signal_emit(&touch.events.down, &ev);
        } else
        {
            wlr_touch_motion_event ev;
            ev.touch     = &touch;
            ev.time_msec = get_current_time();
            ev.x = x;
            ev.y = y;
            ev.touch_id = finger;
            wl_signal_emit(&touch.events.motion, &ev);
        }

        wl_signal_emit(&touch.events.frame, NULL);
    }

    void do_touch_release(int finger)
    {
        wlr_touch_up_event ev;
        ev.touch     = &touch;
        ev.time_msec = get_current_time();
        ev.touch_id  = finger;
        wl_signal_emit(&touch.events.up, &ev);
        wl_signal_emit(&touch.events.frame, NULL);
    }

    void do_tablet_proximity(bool prox_in, double x, double y)
    {
        convert_xy_to_relative(&x, &y);
        wlr_tablet_tool_proximity_event ev;
        ev.tablet = &tablet;
        ev.tool   = &tablet_tool;
        ev.state  = prox_in ? WLR_TABLET_TOOL_PROXIMITY_IN : WLR_TABLET_TOOL_PROXIMITY_OUT;
        ev.time_msec = get_current_time();
        ev.x = x;
        ev.y = y;
        wl_signal_emit(&tablet.events.proximity, &ev);
    }

    void do_tablet_tip(bool tip_down, double x, double y)
    {
        convert_xy_to_relative(&x, &y);
        wlr_tablet_tool_tip_event ev;
        ev.tablet = &tablet;
        ev.tool   = &tablet_tool;
        ev.state  = tip_down ? WLR_TABLET_TOOL_TIP_DOWN : WLR_TABLET_TOOL_TIP_UP;
        ev.time_msec = get_current_time();
        ev.x = x;
        ev.y = y;
        wl_signal_emit(&tablet.events.tip, &ev);
    }

    void do_tablet_button(uint32_t button, bool down)
    {
        wlr_tablet_tool_button_event ev;
        ev.tablet = &tablet;
        ev.tool   = &tablet_tool;
        ev.button = button;
        ev.state  = down ? WLR_BUTTON_PRESSED : WLR_BUTTON_RELEASED;
        ev.time_msec = get_current_time();
        wl_signal_emit(&tablet.events.button, &ev);
    }

    void do_tablet_axis(double x, double y, double pressure)
    {
        convert_xy_to_relative(&x, &y);
        wlr_tablet_tool_axis_event ev;
        ev.tablet = &tablet;
        ev.tool   = &tablet_tool;
        ev.time_msec = get_current_time();
        ev.pressure  = pressure;
        ev.x = x;
        ev.y = y;

        ev.updated_axes = WLR_TABLET_TOOL_AXIS_X | WLR_TABLET_TOOL_AXIS_Y | WLR_TABLET_TOOL_AXIS_PRESSURE;
        wl_signal_emit(&tablet.events.axis, &ev);
    }

    void do_tablet_pad_button(uint32_t button, bool state)
    {
        wlr_tablet_pad_button_event ev;
        ev.group  = 0;
        ev.button = button;
        ev.state  = state ? WLR_BUTTON_PRESSED : WLR_BUTTON_RELEASED;
        ev.mode   = 0;
        ev.time_msec = get_current_time();
        wl_signal_emit(&tablet_pad.events.button, &ev);
    }

    headless_input_backend_t(const headless_input_backend_t&) = delete;
    headless_input_backend_t(headless_input_backend_t&&) = delete;
    headless_input_backend_t& operator =(const headless_input_backend_t&) = delete;
    headless_input_backend_t& operator =(headless_input_backend_t&&) = delete;
};

static inline nlohmann::json get_ok()
{
    return nlohmann::json{
        {"result", "ok"}
    };
}

static inline nlohmann::json get_error(std::string msg)
{
    return nlohmann::json{
        {"error", std::string(msg)}
    };
}

class ipc_plugin_t : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        input = std::make_unique<headless_input_backend_t>();

        char *pre_socket   = getenv("_WAYFIRE_SOCKET");
        const auto& dname  = wf::get_core().wayland_display;
        std::string socket = pre_socket ?: "/tmp/wayfire-" + dname + ".socket";
        setenv("WAYFIRE_SOCKET", socket.c_str(), 1);

        server = std::make_unique<ipc::server_t>(socket);
        server->register_method("core/list_views", list_views);
        server->register_method("core/create_wayland_output", create_wayland_output);
        server->register_method("core/destroy_wayland_output",
            destroy_wayland_output);
        server->register_method("core/feed_key", feed_key);
        server->register_method("core/feed_button", feed_button);
        server->register_method("core/move_cursor", move_cursor);
        server->register_method("core/run", run);
        server->register_method("core/ping", ping);
        server->register_method("core/get_display", get_display);
        server->register_method("core/layout_views", layout_views);
        server->register_method("core/touch", do_touch);
        server->register_method("core/touch_release", do_touch_release);

        server->register_method("core/tablet/tool_proximity", do_tool_proximity);
        server->register_method("core/tablet/tool_button", do_tool_button);
        server->register_method("core/tablet/tool_axis", do_tool_axis);
        server->register_method("core/tablet/tool_tip", do_tool_tip);
        server->register_method("core/tablet/pad_button", do_pad_button);
    }

    bool is_unloadable() override
    {
        return false;
    }

    using method_t = ipc::server_t::method_cb;

    method_t list_views = [] (nlohmann::json)
    {
        auto response = nlohmann::json::array();

        for (auto& view : wf::get_core().get_all_views())
        {
            nlohmann::json v;
            v["id"]     = view->get_id();
            v["title"]  = view->get_title();
            v["app-id"] = view->get_app_id();
            v["geometry"] = geometry_to_json(view->get_wm_geometry());
            v["base-geometry"] = geometry_to_json(view->get_output_geometry());
            v["state"] = {
                {"tiled", view->tiled_edges},
                {"fullscreen", view->fullscreen},
                {"minimized", view->minimized},
            };

            uint32_t layer = -1;
            if (view->get_output())
            {
                layer = view->get_output()->workspace->get_view_layer(view);
            }

            v["layer"] = layer_to_string(layer);

            response.push_back(v);
        }

        return response;
    };

    method_t layout_views = [] (nlohmann::json data)
    {
        auto views = wf::get_core().get_all_views();
        EXPECT_FIELD(data, "views", array);
        for (auto v : data["views"])
        {
            EXPECT_FIELD(v, "id", number);
            EXPECT_FIELD(v, "x", number);
            EXPECT_FIELD(v, "y", number);
            EXPECT_FIELD(v, "width", number);
            EXPECT_FIELD(v, "height", number);

            auto it = std::find_if(views.begin(), views.end(), [&] (auto& view)
            {
                return view->get_id() == v["id"];
            });

            if (it == views.end())
            {
                return get_error("Could not find view with id " +
                    std::to_string((int)v["id"]));
            }

            if (v.contains("output"))
            {
                EXPECT_FIELD(v, "output", string);
                auto wo = wf::get_core().output_layout->find_output(v["output"]);
                if (!wo)
                {
                    return get_error("Unknown output " + (std::string)v["output"]);
                }

                wf::get_core().move_view_to_output(*it, wo, false);
            }

            wf::geometry_t g{v["x"], v["y"], v["width"], v["height"]};
            (*it)->set_geometry(g);
        }

        return get_ok();
    };

    method_t create_wayland_output = [] (nlohmann::json)
    {
        auto backend = wf::get_core().backend;

        wlr_backend *wayland_backend = NULL;
        wlr_multi_for_each_backend(backend, locate_wayland_backend,
            &wayland_backend);

        if (!wayland_backend)
        {
            return get_error("Wayfire is not running in nested wayland mode!");
        }

        wlr_wl_output_create(wayland_backend);
        return get_ok();
    };

    method_t destroy_wayland_output = [] (nlohmann::json data)
    {
        EXPECT_FIELD(data, "output", string);
        auto output = wf::get_core().output_layout->find_output(data["output"]);
        if (!output)
        {
            return get_error("Could not find output: \"" +
                (std::string)data["output"] + "\"");
        }

        wlr_output_destroy(output->handle);
        return get_ok();
    };

    struct key_t
    {
        bool modifier;
        int code;
    };

    std::variant<key_t, std::string> parse_key(nlohmann::json data)
    {
        if (!data.count("combo") || !data["combo"].is_string())
        {
            return std::string("Missing or wrong json type for `combo`!");
        }

        std::string combo = data["combo"];
        if (combo.size() < 4)
        {
            return std::string("Missing or wrong json type for `combo`!");
        }

        // Check super modifier
        bool modifier = false;
        if (combo.substr(0, 2) == "S-")
        {
            modifier = true;
            combo    = combo.substr(2);
        }

        int key = libevdev_event_code_from_name(EV_KEY, combo.c_str());
        if (key == -1)
        {
            return std::string("Failed to parse combo \"" + combo + "\"");
        }

        return key_t{modifier, key};
    }

    method_t feed_key = [=] (nlohmann::json data)
    {
        EXPECT_FIELD(data, "key", string);
        EXPECT_FIELD(data, "state", boolean);

        std::string key = data["key"];
        int keycode     = libevdev_event_code_from_name(EV_KEY, key.c_str());
        if (keycode == -1)
        {
            return get_error("Failed to parse evdev key \"" + key + "\"");
        }

        if (data["state"])
        {
            input->do_key(keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
        } else
        {
            input->do_key(keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
        }

        return get_ok();
    };

    method_t feed_button = [=] (nlohmann::json data)
    {
        auto result = parse_key(data);
        auto button = std::get_if<key_t>(&result);
        if (!button)
        {
            return get_error(std::get<std::string>(result));
        }

        if (!data.count("mode") || !data["mode"].is_string())
        {
            return get_error("No mode specified");
        }

        auto mode = data["mode"];

        if ((mode == "press") || (mode == "full"))
        {
            if (button->modifier)
            {
                input->do_key(KEY_LEFTMETA, WL_KEYBOARD_KEY_STATE_PRESSED);
            }

            input->do_button(button->code, WLR_BUTTON_PRESSED);
        }

        if ((mode == "release") || (mode == "full"))
        {
            input->do_button(button->code, WLR_BUTTON_RELEASED);
            if (button->modifier)
            {
                input->do_key(KEY_LEFTMETA, WL_KEYBOARD_KEY_STATE_RELEASED);
            }
        }

        return get_ok();
    };

    method_t move_cursor = [=] (nlohmann::json data)
    {
        if (!data.count("x") || !data.count("y") ||
            !data["x"].is_number() || !data["y"].is_number())
        {
            return get_error("Move cursor needs double x/y arguments");
        }

        double x = data["x"];
        double y = data["y"];
        input->do_motion(x, y);
        return get_ok();
    };

    method_t do_touch = [=] (nlohmann::json data)
    {
        EXPECT_FIELD(data, "finger", number_integer);
        EXPECT_FIELD(data, "x", number);
        EXPECT_FIELD(data, "y", number);

        input->do_touch(data["finger"], data["x"], data["y"]);
        return get_ok();
    };

    method_t do_touch_release = [=] (nlohmann::json data)
    {
        EXPECT_FIELD(data, "finger", number_integer);
        input->do_touch_release(data["finger"]);
        return get_ok();
    };

    method_t run = [=] (nlohmann::json data)
    {
        if (!data.count("cmd") || !data["cmd"].is_string())
        {
            return get_error("run command needs a cmd to run");
        }

        auto response = get_ok();
        response["pid"] = wf::get_core().run(data["cmd"]);
        return response;
    };

    method_t ping = [=] (nlohmann::json data)
    {
        return get_ok();
    };

    method_t get_display = [=] (nlohmann::json data)
    {
        nlohmann::json dpy;
        dpy["wayland"]  = wf::get_core().wayland_display;
        dpy["xwayland"] = wf::get_core().get_xwayland_display();
        return dpy;
    };

    method_t do_tool_proximity = [=] (nlohmann::json data)
    {
        EXPECT_FIELD(data, "proximity_in", boolean);
        EXPECT_FIELD(data, "x", number);
        EXPECT_FIELD(data, "y", number);
        input->do_tablet_proximity(data["proximity_in"], data["x"], data["y"]);
        return get_ok();
    };

    method_t do_tool_button = [=] (nlohmann::json data)
    {
        EXPECT_FIELD(data, "button", number_integer);
        EXPECT_FIELD(data, "state", boolean);
        input->do_tablet_button(data["button"], data["state"]);
        return get_ok();
    };

    method_t do_tool_axis = [=] (nlohmann::json data)
    {
        EXPECT_FIELD(data, "x", number);
        EXPECT_FIELD(data, "y", number);
        EXPECT_FIELD(data, "pressure", number);
        input->do_tablet_axis(data["x"], data["y"], data["pressure"]);
        return get_ok();
    };

    method_t do_tool_tip = [=] (nlohmann::json data)
    {
        EXPECT_FIELD(data, "x", number);
        EXPECT_FIELD(data, "y", number);
        EXPECT_FIELD(data, "state", boolean);
        input->do_tablet_tip(data["state"], data["x"], data["y"]);
        return get_ok();
    };

    method_t do_pad_button = [=] (nlohmann::json data)
    {
        EXPECT_FIELD(data, "button", number_integer);
        EXPECT_FIELD(data, "state", boolean);
        input->do_tablet_pad_button(data["button"], data["state"]);
        return get_ok();
    };

    std::unique_ptr<ipc::server_t> server;
    std::unique_ptr<headless_input_backend_t> input;
};
}

DECLARE_WAYFIRE_PLUGIN(wf::ipc_plugin_t);
