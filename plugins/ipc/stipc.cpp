#include <wayfire/singleton-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/output-layout.hpp>
#include <getopt.h>
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
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_output_layout.h>
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

class headless_input_backend_t
{
  public:
    wlr_backend *backend;
    wlr_input_device *pointer;
    wlr_input_device *keyboard;
    wlr_input_device *touch;

    headless_input_backend_t()
    {
        auto& core = wf::get_core();
        backend = wlr_headless_backend_create(core.display);
        wlr_multi_backend_add(core.backend, backend);

        pointer  = wlr_headless_add_input_device(backend, WLR_INPUT_DEVICE_POINTER);
        keyboard = wlr_headless_add_input_device(backend, WLR_INPUT_DEVICE_KEYBOARD);
        touch    = wlr_headless_add_input_device(backend, WLR_INPUT_DEVICE_TOUCH);

        if (core.get_current_state() == compositor_state_t::RUNNING)
        {
            wlr_backend_start(backend);
        }
    }

    ~headless_input_backend_t()
    {
        auto& core = wf::get_core();
        wlr_multi_backend_remove(core.backend, backend);
        wlr_backend_destroy(backend);
    }

    void do_key(uint32_t key, wl_keyboard_key_state state)
    {
        wlr_event_keyboard_key ev;
        ev.keycode = key;
        ev.state   = state;
        ev.update_state = true;
        ev.time_msec    = get_current_time();
        wlr_keyboard_notify_key(keyboard->keyboard, &ev);
    }

    void do_button(uint32_t button, wlr_button_state state)
    {
        wlr_event_pointer_button ev;
        ev.device    = pointer;
        ev.button    = button;
        ev.state     = state;
        ev.time_msec = get_current_time();
        wl_signal_emit(&pointer->pointer->events.button, &ev);
        wl_signal_emit(&pointer->pointer->events.frame, NULL);
    }

    void do_motion(double x, double y)
    {
        auto cursor = wf::get_core().get_cursor_position();

        wlr_event_pointer_motion ev;
        ev.device    = pointer;
        ev.time_msec = get_current_time();
        ev.delta_x   = ev.unaccel_dx = x - cursor.x;
        ev.delta_y   = ev.unaccel_dy = y - cursor.y;
        wl_signal_emit(&pointer->pointer->events.motion, &ev);
        wl_signal_emit(&pointer->pointer->events.frame, NULL);
    }

    void do_touch(int finger, double x, double y)
    {
        auto layout = wf::get_core().output_layout->get_handle();
        auto box    = wlr_output_layout_get_box(layout, NULL);

        if (!wf::get_core().get_touch_state().fingers.count(finger))
        {
            wlr_event_touch_down ev;
            ev.device    = touch;
            ev.time_msec = get_current_time();
            ev.x = 1.0 * (x - box->x) / box->width;
            ev.y = 1.0 * (y - box->y) / box->height;
            ev.touch_id = finger;
            wl_signal_emit(&touch->touch->events.down, &ev);
        } else
        {
            wlr_event_touch_motion ev;
            ev.device    = touch;
            ev.time_msec = get_current_time();
            ev.x = 1.0 * (x - box->x) / box->width;
            ev.y = 1.0 * (y - box->y) / box->height;
            ev.touch_id = finger;
            wl_signal_emit(&touch->touch->events.motion, &ev);
        }

        wl_signal_emit(&touch->touch->events.frame, NULL);
    }

    void do_touch_release(int finger)
    {
        wlr_event_touch_up ev;
        ev.device    = touch;
        ev.time_msec = get_current_time();
        ev.touch_id  = finger;
        wl_signal_emit(&touch->touch->events.up, &ev);
        wl_signal_emit(&touch->touch->events.frame, NULL);
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

class ipc_plugin_t
{
  public:
    ipc_plugin_t()
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

    std::unique_ptr<ipc::server_t> server;
    std::unique_ptr<headless_input_backend_t> input;
};
}

DECLARE_WAYFIRE_PLUGIN((wf::singleton_plugin_t<wf::ipc_plugin_t, false>));
