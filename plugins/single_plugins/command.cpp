#include "plugins/ipc/ipc-helpers.hpp"
#include "wayfire/bindings.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include "wayfire/signal-provider.hpp"
#include <cstdint>
#include <list>
#include <wayfire/config/option-types.hpp>
#include <wayfire/config/types.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/util/log.hpp>
#include <plugins/ipc/ipc-method-repository.hpp>
#include <list>

/* Initial repeat delay passed */
static int repeat_delay_timeout_handler(void *callback)
{
    (*reinterpret_cast<std::function<void()>*>(callback))();

    return 1; // disconnect
}

/* Between each repeat */
static int repeat_once_handler(void *callback)
{
    (*reinterpret_cast<std::function<void()>*>(callback))();

    return 1; // continue timer
}

/* Provides a way to bind specific commands to activator bindings.
 *
 * It supports 4 modes:
 *
 * 1. Regular bindings
 * 2. Repeatable bindings - for example, if the user binds a keybinding, then
 * after a specific delay the command begins to be executed repeatedly, until
 * the user released the key. In the config file, repeatable bindings have the
 * prefix repeatable_
 * 3. Always bindings - bindings that can be executed even if a plugin is already
 * active, or if the screen is locked. They have a prefix always_
 * 4. Release bindings - Execute a command when a binding is released. Useful for
 * Push-To-Talk. They have a prefix release_
 * */

class wayfire_command : public wf::plugin_interface_t
{
    std::vector<wf::activator_callback> bindings;

    struct ipc_binding_t
    {
        wf::activator_callback callback;
        wf::ipc::client_interface_t *client;
    };

    static inline uint64_t binding_to_id(const ipc_binding_t& binding)
    {
        return (std::uintptr_t)(&binding.callback);
    }

    std::list<ipc_binding_t> ipc_bindings;
    using command_callback = std::function<bool ()>;

    struct
    {
        uint32_t pressed_button = 0;
        uint32_t pressed_key    = 0;
        command_callback callback;
    } repeat;

    wl_event_source *repeat_source = NULL;
    wl_event_source *repeat_delay_source = NULL;

    enum binding_mode
    {
        BINDING_NORMAL,
        BINDING_REPEAT,
        BINDING_RELEASE,
    };

    bool on_binding(command_callback callback, binding_mode mode, bool exec_always,
        const wf::activator_data_t& data)
    {
        /* We already have a repeatable command, do not accept further bindings */
        if (repeat.pressed_key || repeat.pressed_button)
        {
            return false;
        }

        auto focused_output = wf::get_core().seat->get_active_output();
        if (!exec_always && !focused_output->can_activate_plugin(&grab_interface))
        {
            return false;
        }

        if (mode == BINDING_RELEASE)
        {
            repeat.callback = callback;
            if ((data.source == wf::activator_source_t::KEYBINDING) ||
                (data.source == wf::activator_source_t::MODIFIERBINDING))
            {
                repeat.pressed_key = data.activation_data;
                wf::get_core().connect(&on_key_event_release);
            } else
            {
                repeat.pressed_button = data.activation_data;
                wf::get_core().connect(&on_button_event_release);
            }

            return true;
        } else
        {
            callback();
        }

        /* No repeat necessary in any of those cases */
        if ((mode != BINDING_REPEAT) ||
            (data.source == wf::activator_source_t::GESTURE) ||
            (data.activation_data == 0))
        {
            return true;
        }

        repeat.callback = callback;
        if (data.source == wf::activator_source_t::KEYBINDING)
        {
            repeat.pressed_key = data.activation_data;
        } else
        {
            repeat.pressed_button = data.activation_data;
        }

        repeat_delay_source = wl_event_loop_add_timer(wf::get_core().ev_loop,
            repeat_delay_timeout_handler, &on_repeat_delay_timeout);

        wl_event_source_timer_update(repeat_delay_source, wf::option_wrapper_t<int>("input/kb_repeat_delay"));

        wf::get_core().connect(&on_button_event);
        wf::get_core().connect(&on_key_event);
        return true;
    }

    std::function<void()> on_repeat_delay_timeout = [=] ()
    {
        repeat_delay_source = NULL;
        repeat_source = wl_event_loop_add_timer(wf::get_core().ev_loop, repeat_once_handler, &on_repeat_once);
        on_repeat_once();
    };

    std::function<void()> on_repeat_once = [=] ()
    {
        uint32_t repeat_rate = wf::option_wrapper_t<int>("input/kb_repeat_rate");
        if ((repeat_rate <= 0) || (repeat_rate > 1000))
        {
            return reset_repeat();
        }

        wl_event_source_timer_update(repeat_source, 1000 / repeat_rate);
        repeat.callback();
    };

    void reset_repeat()
    {
        if (repeat_delay_source)
        {
            wl_event_source_remove(repeat_delay_source);
            repeat_delay_source = NULL;
        }

        if (repeat_source)
        {
            wl_event_source_remove(repeat_source);
            repeat_source = NULL;
        }

        repeat.pressed_key = repeat.pressed_button = 0;
        on_button_event.disconnect();
        on_key_event.disconnect();
    }

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_button_event>> on_button_event =
        [=] (wf::input_event_signal<wlr_pointer_button_event> *ev)
    {
        if ((ev->event->button == repeat.pressed_button) &&
            (ev->event->state == WL_POINTER_BUTTON_STATE_RELEASED))
        {
            reset_repeat();
        }
    };

    wf::signal::connection_t<wf::input_event_signal<wlr_keyboard_key_event>> on_key_event =
        [=] (wf::input_event_signal<wlr_keyboard_key_event> *ev)
    {
        if ((ev->event->keycode == repeat.pressed_key) &&
            (ev->event->state == WL_KEYBOARD_KEY_STATE_RELEASED))
        {
            reset_repeat();
        }
    };

    wf::signal::connection_t<wf::input_event_signal<wlr_keyboard_key_event>> on_key_event_release =
        [=] (wf::input_event_signal<wlr_keyboard_key_event> *ev)
    {
        if ((ev->event->keycode == repeat.pressed_key) &&
            (ev->event->state == WL_KEYBOARD_KEY_STATE_RELEASED))
        {
            repeat.callback();
            repeat.pressed_key = repeat.pressed_button = 0;
            on_key_event_release.disconnect();
        }
    };

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_button_event>> on_button_event_release =
        [=] (wf::input_event_signal<wlr_pointer_button_event> *ev)
    {
        if ((ev->event->button == repeat.pressed_button) &&
            (ev->event->state == WL_POINTER_BUTTON_STATE_RELEASED))
        {
            repeat.callback();
            repeat.pressed_key = repeat.pressed_button = 0;
            on_button_event_release.disconnect();
        }
    };

    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> method_repository;

  public:
    wf::option_wrapper_t<wf::config::compound_list_t<
        std::string, wf::activatorbinding_t>> regular_bindings{"command/bindings"};

    wf::option_wrapper_t<wf::config::compound_list_t<std::string, wf::activatorbinding_t>> repeat_bindings{
        "command/repeatable_bindings"
    };

    wf::option_wrapper_t<wf::config::compound_list_t<std::string, wf::activatorbinding_t>> always_bindings{
        "command/always_bindings"
    };

    wf::option_wrapper_t<wf::config::compound_list_t<std::string, wf::activatorbinding_t>> release_bindings{
        "command/release_bindings"
    };

    std::function<void()> setup_bindings_from_config = [=] ()
    {
        clear_bindings();
        using namespace std::placeholders;

        auto regular    = regular_bindings.value();
        auto repeatable = repeat_bindings.value();
        auto always     = always_bindings.value();
        auto release    = release_bindings.value();
        bindings.resize(
            regular.size() + repeatable.size() + always.size() + release.size());
        size_t i = 0;

        const auto& push_bindings =
            [&] (wf::config::compound_list_t<std::string, wf::activatorbinding_t>& list, binding_mode mode,
                 bool always_exec = false)
        {
            for (const auto& [_, _cmd, activator] : list)
            {
                std::string cmd     = _cmd;
                command_callback cb = [cmd] () -> bool { return wf::get_core().run(cmd); };
                bindings[i] =
                    std::bind(std::mem_fn(&wayfire_command::on_binding), this, cb, mode, always_exec, _1);
                wf::get_core().bindings->add_activator(wf::create_option(activator), &bindings[i]);
                ++i;
            }
        };

        push_bindings(regular, BINDING_NORMAL);
        push_bindings(repeatable, BINDING_REPEAT);
        push_bindings(always, BINDING_NORMAL, true);
        push_bindings(release, BINDING_RELEASE);
    };

    void clear_bindings()
    {
        for (auto& binding : bindings)
        {
            wf::get_core().bindings->rem_binding(&binding);
        }

        bindings.clear();
    }

    wf::signal::connection_t<wf::reload_config_signal> on_reload_config = [=] (auto)
    {
        setup_bindings_from_config();
    };

    wf::plugin_activation_data_t grab_interface = {
        .name = "command",
        .capabilities = wf::CAPABILITY_GRAB_INPUT,
    };

    void init()
    {
        using namespace std::placeholders;
        setup_bindings_from_config();
        wf::get_core().connect(&on_reload_config);

        method_repository->connect(&on_client_disconnect);
        method_repository->register_method("command/register-binding", on_register_binding);
        method_repository->register_method("command/unregister-binding", on_unregister_binding);
        method_repository->register_method("command/clear-bindings", on_clear_ipc_bindings);
    }

    void fini()
    {
        method_repository->unregister_method("command/register-binding");
        method_repository->unregister_method("command/unregister-binding");
        method_repository->unregister_method("command/clear-bindings");
        clear_bindings();
    }

    wf::ipc::method_callback_full on_register_binding =
        [&] (const wf::json_t& js, wf::ipc::client_interface_t *client)
    {
        auto binding_str = wf::ipc::json_get_string(js, "binding");
        auto mode_str    = wf::ipc::json_get_optional_string(js, "mode");
        auto exec_always = wf::ipc::json_get_optional_bool(js, "exec-always").value_or(false);
        auto call_method = wf::ipc::json_get_optional_string(js, "call-method");
        auto command     = wf::ipc::json_get_optional_string(js, "command");

        if (call_method.has_value() && !js.has_member("call-data"))
        {
            return wf::ipc::json_error("call-method requires call-data!");
        }

        auto binding = wf::option_type::from_string<wf::activatorbinding_t>(binding_str);
        if (!binding)
        {
            return wf::ipc::json_error("Invalid binding!");
        }

        binding_mode mode = BINDING_NORMAL;
        if (mode_str.has_value())
        {
            if (mode_str == "release")
            {
                mode = BINDING_RELEASE;
            } else if (mode_str == "repeat")
            {
                mode = BINDING_REPEAT;
            } else
            {
                return wf::ipc::json_error("Invalid mode!");
            }
        }

        ipc_bindings.push_back({});

        uint64_t id = binding_to_id(ipc_bindings.back());

        wf::activator_callback act_callback;
        bool temporary_binding = false;

        if (call_method.has_value())
        {
            act_callback = [=] (const wf::activator_data_t& data)
            {
                return on_binding([js, this] () -> bool
                {
                    method_repository->call_method(js["call-method"], js["call-data"]);
                    return true;
                }, mode, exec_always, data);
            };
        } else if (command.has_value())
        {
            act_callback = [=] (const wf::activator_data_t& data)
            {
                return on_binding([js] () -> bool
                {
                    return wf::get_core().run(js["command"]);
                }, mode, exec_always, data);
            };
        } else
        {
            temporary_binding = true;
            act_callback = [=] (const wf::activator_data_t& data)
            {
                return on_binding([client, id] () -> bool
                {
                    wf::json_t event;
                    event["event"] = "command-binding";
                    event["binding-id"] = id;
                    return client->send_json(event);
                }, mode, exec_always, data);
            };
        }

        ipc_bindings.back().callback = act_callback;
        ipc_bindings.back().client   = temporary_binding ? client : NULL;
        wf::get_core().bindings->add_activator(wf::create_option(*binding), &ipc_bindings.back().callback);

        wf::json_t response = wf::ipc::json_ok();
        response["binding-id"] = id;
        return response;
    };

    wf::ipc::method_callback on_unregister_binding = [&] (const wf::json_t& js)
    {
        auto binding_id = wf::ipc::json_get_uint64(js, "binding-id");
        ipc_bindings.remove_if([&] (const ipc_binding_t& binding)
        {
            if (binding_to_id(binding) == binding_id)
            {
                wf::get_core().bindings->rem_binding((void*)&binding.callback);
                return true;
            }

            return false;
        });

        return wf::ipc::json_ok();
    };

    void clear_ipc_bindings(std::function<bool(const ipc_binding_t&)> filter)
    {
        ipc_bindings.remove_if([&] (const ipc_binding_t& binding)
        {
            if (filter(binding))
            {
                wf::get_core().bindings->rem_binding((void*)&binding.callback);
                return true;
            }

            return false;
        });
    }

    wf::ipc::method_callback on_clear_ipc_bindings = [&] (const wf::json_t& js)
    {
        clear_ipc_bindings([&] (const ipc_binding_t& binding)
        {
            return binding.client == nullptr;
        });

        return wf::ipc::json_ok();
    };

    wf::signal::connection_t<wf::ipc::client_disconnected_signal> on_client_disconnect =
        [=] (wf::ipc::client_disconnected_signal *ev)
    {
        clear_ipc_bindings([&] (const ipc_binding_t& binding)
        {
            return binding.client == ev->client;
        });
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_command);
