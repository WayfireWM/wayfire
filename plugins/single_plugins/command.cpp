#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util/log.hpp>

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
 * It supports 2 modes:
 *
 * 1. Regular bindings
 * 2. Repeatable bindings - for example, if the user binds a keybinding, then
 * after a specific delay the command begins to be executed repeatedly, until
 * the user released the key. In the config file, repeatable bindings have the
 * prefix repeatable_
 * 3. Always bindings - bindings that can be executed even if a plugin is already
 * active, or if the screen is locked. They have a prefix always_
 * */

class wayfire_command : public wf::plugin_interface_t
{
    std::vector<wf::activator_callback> bindings;

    struct
    {
        uint32_t pressed_button = 0;
        uint32_t pressed_key    = 0;
        std::string repeat_command;
    } repeat;

    wl_event_source *repeat_source = NULL, *repeat_delay_source = NULL;

    enum binding_mode
    {
        BINDING_NORMAL,
        BINDING_REPEAT,
        BINDING_ALWAYS,
    };

    bool on_binding(std::string command, binding_mode mode,
        const wf::activator_data_t& data)
    {
        /* We already have a repeatable command, do not accept further bindings */
        if (repeat.pressed_key || repeat.pressed_button)
        {
            return false;
        }

        uint32_t act_flags = 0;
        if (mode == BINDING_ALWAYS)
        {
            act_flags |= wf::PLUGIN_ACTIVATION_IGNORE_INHIBIT;
        }

        if (!output->activate_plugin(grab_interface, act_flags))
        {
            return false;
        }

        wf::get_core().run(command.c_str());

        /* No repeat necessary in any of those cases */
        if ((mode != BINDING_REPEAT) ||
            (data.source == wf::activator_source_t::GESTURE) ||
            (data.activation_data == 0))
        {
            output->deactivate_plugin(grab_interface);

            return true;
        }

        repeat.repeat_command = command;
        if (data.source == wf::activator_source_t::KEYBINDING)
        {
            repeat.pressed_key = data.activation_data;
        } else
        {
            repeat.pressed_button = data.activation_data;
        }

        repeat_delay_source = wl_event_loop_add_timer(wf::get_core().ev_loop,
            repeat_delay_timeout_handler, &on_repeat_delay_timeout);

        wl_event_source_timer_update(repeat_delay_source,
            wf::option_wrapper_t<int>("input/kb_repeat_delay"));

        wf::get_core().connect_signal("pointer_button", &on_button_event);
        wf::get_core().connect_signal("keyboard_key", &on_key_event);

        return true;
    }

    std::function<void()> on_repeat_delay_timeout = [=] ()
    {
        repeat_delay_source = NULL;
        repeat_source = wl_event_loop_add_timer(wf::get_core().ev_loop,
            repeat_once_handler, &on_repeat_once);
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
        wf::get_core().run(repeat.repeat_command.c_str());
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
        output->deactivate_plugin(grab_interface);

        wf::get_core().disconnect_signal("pointer_button", &on_button_event);
        wf::get_core().disconnect_signal("keyboard_key", &on_key_event);
    }

    wf::signal_callback_t on_button_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_pointer_button_event>*>(data);
        if ((ev->event->button == repeat.pressed_button) &&
            (ev->event->state == WLR_BUTTON_RELEASED))
        {
            reset_repeat();
        }
    };

    wf::signal_callback_t on_key_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_keyboard_key_event>*>(data);
        if ((ev->event->keycode == repeat.pressed_key) &&
            (ev->event->state == WLR_KEY_RELEASED))
        {
            reset_repeat();
        }
    };

  public:
    wf::option_wrapper_t<wf::config::compound_list_t<
        std::string, wf::activatorbinding_t>> regular_bindings{"command/bindings"};

    wf::option_wrapper_t<wf::config::compound_list_t<
        std::string, wf::activatorbinding_t>> repeat_bindings{
        "command/repeatable_bindings"
    };

    wf::option_wrapper_t<wf::config::compound_list_t<
        std::string, wf::activatorbinding_t>> always_bindings{
        "command/always_bindings"
    };

    std::function<void()> setup_bindings_from_config = [=] ()
    {
        clear_bindings();
        using namespace std::placeholders;

        auto regular    = regular_bindings.value();
        auto repeatable = repeat_bindings.value();
        auto always     = always_bindings.value();
        bindings.resize(regular.size() + repeatable.size() + always.size());
        size_t i = 0;

        const auto& push_bindings = [&] (
            wf::config::compound_list_t<std::string, wf::activatorbinding_t>& list,
            binding_mode mode)
        {
            for (const auto& [_, cmd, activator] : list)
            {
                bindings[i] = std::bind(std::mem_fn(&wayfire_command::on_binding),
                    this, cmd, mode, _1);
                output->add_activator(
                    wf::create_option(activator), &bindings[i]);
                ++i;
            }
        };

        push_bindings(regular, BINDING_NORMAL);
        push_bindings(repeatable, BINDING_REPEAT);
        push_bindings(always, BINDING_ALWAYS);
    };

    void clear_bindings()
    {
        for (auto& binding : bindings)
        {
            output->rem_binding(&binding);
        }

        bindings.clear();
    }

    wf::signal_callback_t reload_config;

    void init()
    {
        grab_interface->name = "command";
        grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;

        using namespace std::placeholders;

        setup_bindings_from_config();
        reload_config = [=] (wf::signal_data_t*)
        {
            setup_bindings_from_config();
        };

        wf::get_core().connect_signal("reload-config", &reload_config);
    }

    void fini()
    {
        wf::get_core().disconnect_signal("reload-config", &reload_config);
        clear_bindings();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_command);
