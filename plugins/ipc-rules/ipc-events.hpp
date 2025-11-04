#pragma once

#include "ipc-rules-common.hpp"
#include <set>
#include "wayfire/output-layout.hpp"
#include "wayfire/plugins/ipc/ipc-method-repository.hpp"
#include "wayfire/seat.hpp"
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>

// private API, used to make it easier to serialize output state
#include "src/core/output-layout-priv.hpp"
#include "wayfire/txn/transaction-manager.hpp"

namespace wf
{
class ipc_rules_events_methods_t : public wf::per_output_tracker_mixin_t<>
{
    static constexpr const char *PRE_MAP_EVENT = "view-pre-map";

  public:
    void init_events(ipc::method_repository_t *method_repository)
    {
        method_repository->register_method("window-rules/events/watch", on_client_watch);
        method_repository->register_method("window-rules/unblock-map", on_client_unblock_map);
        method_repository->connect(&on_client_disconnected);
        method_repository->connect(&on_custom_event);

        signal_map[PRE_MAP_EVENT] = signal_registration_handler{
            .register_core = [=] () { wf::get_core().tx_manager->connect(&on_new_tx); },
            .unregister    = [=] () { on_new_tx.disconnect(); },
            .auto_register = false,
        };

        init_output_tracking();
    }

    void fini_events(ipc::method_repository_t *method_repository)
    {
        method_repository->unregister_method("window-rules/events/watch");
        method_repository->unregister_method("window-rules/unblock-map");
        fini_output_tracking();
    }

    void handle_new_output(wf::output_t *output) override
    {
        for (auto& [_, event] : signal_map)
        {
            if (event.connected_count)
            {
                event.register_output(output);
            }
        }
    }

    void handle_output_removed(wf::output_t *output) override
    {}

    wf::signal::connection_t<wf::ipc_rules::detail::custom_event_signal_t> on_custom_event =
        [=] (wf::ipc_rules::detail::custom_event_signal_t *ev)
    {
        send_event_to_subscribes(ev->data, ev->data["event"].as_string(), true);
    };

    // Template FOO for efficient management of signals: ensure that only actually listened-for signals
    // are connected.
    struct signal_registration_handler
    {
        std::function<void()> register_core = [] () {};
        std::function<void(wf::output_t*)> register_output = [] (wf::output_t*) {};
        std::function<void()> unregister = [] () {};
        int connected_count = 0;
        bool auto_register  = true;

        void increase_count()
        {
            connected_count++;
            if (connected_count > 1)
            {
                return;
            }

            register_core();
            for (auto& wo : wf::get_core().output_layout->get_outputs())
            {
                register_output(wo);
            }
        }

        void decrease_count()
        {
            connected_count--;
            if (connected_count > 0)
            {
                return;
            }

            unregister();
        }
    };

    template<class Signal>
    static signal_registration_handler get_generic_core_registration_cb(
        wf::signal::connection_t<Signal> *conn)
    {
        return {
            .register_core = [=] () { wf::get_core().connect(conn); },
            .unregister    = [=] () { conn->disconnect(); }
        };
    }

    template<class Signal>
    static signal_registration_handler get_generic_output_layout_registration_cb(
        wf::signal::connection_t<Signal> *conn)
    {
        return {
            .register_core = [=] () { wf::get_core().output_layout->connect(conn); },
            .unregister    = [=] () { conn->disconnect(); }
        };
    }

    template<class Signal>
    signal_registration_handler get_generic_output_registration_cb(wf::signal::connection_t<Signal> *conn)
    {
        return {
            .register_output = [=] (wf::output_t *wo) { wo->connect(conn); },
            .unregister = [=] () { conn->disconnect(); }
        };
    }

    std::map<std::string, signal_registration_handler> signal_map =
    {
        {"view-mapped", get_generic_core_registration_cb(&on_view_mapped)},
        {"view-unmapped", get_generic_core_registration_cb(&on_view_unmapped)},
        {"view-set-output", get_generic_core_registration_cb(&on_view_set_output)},
        {"view-geometry-changed", get_generic_core_registration_cb(&on_view_geometry_changed)},
        {"view-wset-changed", get_generic_core_registration_cb(&on_view_moved_to_wset)},
        {"view-focused", get_generic_core_registration_cb(&on_kbfocus_changed)},
        {"view-title-changed", get_generic_core_registration_cb(&on_title_changed)},
        {"view-app-id-changed", get_generic_core_registration_cb(&on_app_id_changed)},
        {"plugin-activation-state-changed", get_generic_core_registration_cb(&on_plugin_activation_changed)},
        {"output-gain-focus", get_generic_core_registration_cb(&on_output_gain_focus)},
        {"keyboard-modifier-state-changed", get_generic_core_registration_cb(&on_keyboard_modifiers)},

        {"output-added", get_generic_output_registration_cb(&on_output_added)},
        {"output-removed", get_generic_output_layout_registration_cb(&on_output_removed)},
        {"output-layout-changed", get_generic_output_layout_registration_cb(&on_output_layout_changed)},

        {"view-tiled", get_generic_output_registration_cb(&_tiled)},
        {"view-minimized", get_generic_output_registration_cb(&_minimized)},
        {"view-fullscreen", get_generic_output_registration_cb(&_fullscreened)},
        {"view-sticky", get_generic_output_registration_cb(&_stickied)},
        {"view-workspace-changed", get_generic_output_registration_cb(&_view_workspace)},
        {"output-wset-changed", get_generic_output_registration_cb(&on_wset_changed)},
        {"wset-workspace-changed", get_generic_output_registration_cb(&on_wset_workspace_changed)},
    };

    struct client_watch_state_t
    {
        std::set<std::string> connected_events;
        bool connected_all = false;
    };

    // Track a list of clients which have requested watch
    std::map<wf::ipc::client_interface_t*, client_watch_state_t> clients;

    wf::ipc::method_callback_full on_client_watch =
        [=] (wf::json_t data, wf::ipc::client_interface_t *client)
    {
        static constexpr const char *EVENTS = "events";
        if (data.has_member(EVENTS) && !data[EVENTS].is_array())
        {
            return wf::ipc::json_error("Event list is not an array!");
        }

        if (clients.count(client))
        {
            return wf::ipc::json_error("Client is already watching events!");
        }

        client_watch_state_t state;
        if (data.has_member(EVENTS))
        {
            for (size_t i = 0; i < data[EVENTS].size(); i++)
            {
                const auto& sub = data[EVENTS][i];
                if (!sub.is_string())
                {
                    return wf::ipc::json_error("Event list contains non-string entries!");
                }

                const auto& event_name = sub.as_string();
                const bool is_known_builtin_event = signal_map.count(event_name);
                const bool is_custom_event = !event_name.empty() && event_name.back() == '#';
                if (is_known_builtin_event || is_custom_event)
                {
                    state.connected_events.insert(sub.as_string());
                } else
                {
                    return wf::ipc::json_error("Event not found: \"" + sub.as_string() + "\"");
                }
            }
        } else
        {
            state.connected_all = true;
            for (auto& [ev_name, ev] : signal_map)
            {
                if (ev.auto_register)
                {
                    state.connected_events.insert(ev_name);
                }
            }
        }

        for (auto& ev_name : state.connected_events)
        {
            signal_map[ev_name].increase_count();
        }

        clients[client] = std::move(state);
        return wf::ipc::json_ok();
    };

    wf::signal::connection_t<wf::ipc::client_disconnected_signal> on_client_disconnected =
        [=] (wf::ipc::client_disconnected_signal *ev)
    {
        for (auto& ev_name : clients[ev->client].connected_events)
        {
            signal_map[ev_name].decrease_count();
        }

        clients.erase(ev->client);
    };

    void send_view_to_subscribes(wayfire_view view, std::string event_name)
    {
        wf::json_t event;
        event["event"] = event_name;
        event["view"]  = ipc_rules::view_to_json(view);
        send_event_to_subscribes(event, event_name);
    }

    void send_event_to_subscribes(const wf::json_t& data, const std::string& event_name,
        bool custom_event = false)
    {
        for (auto& [client, state] : clients)
        {
            if (state.connected_events.empty() || state.connected_events.count(event_name) ||
                (custom_event && state.connected_all))
            {
                client->send_json(data);
            }
        }
    }

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-mapped");
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-unmapped");
    };

    wf::signal::connection_t<wf::view_set_output_signal> on_view_set_output =
        [=] (wf::view_set_output_signal *ev)
    {
        wf::json_t data;
        data["event"]  = "view-set-output";
        data["output"] = ipc_rules::output_to_json(ev->output);
        data["view"]   = ipc_rules::view_to_json(ev->view);
        send_event_to_subscribes(data, data["event"]);
    };

    // required by handle_new_output
    wf::signal::connection_t<wf::output_added_signal> on_output_added =
        [=] (wf::output_added_signal *ev)
    {
        wf::json_t data;
        data["event"]  = "output-added";
        data["output"] = ipc_rules::output_to_json(ev->output);
        send_event_to_subscribes(data, data["event"]);
    };

    wf::signal::connection_t<wf::output_removed_signal> on_output_removed =
        [=] (wf::output_removed_signal *ev)
    {
        wf::json_t data;
        data["event"]  = "output-removed";
        data["output"] = ipc_rules::output_to_json(ev->output);
        send_event_to_subscribes(data, data["event"]);
    };

    wf::signal::connection_t<wf::output_layout_configuration_changed_signal> on_output_layout_changed =
        [=] (wf::output_layout_configuration_changed_signal *ev)
    {
        auto config = wf::get_core().output_layout->get_current_configuration();
        wf::json_t data;
        data["event"] = "output-layout-changed";
        data["configuration"] = wf::json_t::array();

        for (auto& [output, state] : config)
        {
            wf::json_t json_state;
            json_state["name"] = nonull(output->name);

            auto wo = wf::get_core().output_layout->find_output(output);
            json_state["output-id"] = wo ? (int)wo->get_id() : -1;

            json_state["source"] = std::string(layout_detail::get_output_source_name(state.source));
            json_state["depth"]  = state.depth;
            json_state["scale"]  = state.scale;
            json_state["vrr"]    = state.vrr;
            json_state["transform"]   = layout_detail::wl_transform_to_string(state.transform);
            json_state["mirror-from"] = state.mirror_from;

            json_state["position"] = wf::json_t{};
            json_state["position"]["x"] = state.position.get_x();
            json_state["position"]["y"] = state.position.get_y();

            json_state["mode"] = wf::json_t{};
            json_state["mode"]["width"]   = state.mode.width;
            json_state["mode"]["height"]  = state.mode.height;
            json_state["mode"]["refresh"] = state.mode.refresh;
            data["configuration"].append(json_state);
        }

        send_event_to_subscribes(data, data["event"]);
    };

    wf::signal::connection_t<wf::view_geometry_changed_signal> on_view_geometry_changed =
        [=] (wf::view_geometry_changed_signal *ev)
    {
        wf::json_t data;
        data["event"] = "view-geometry-changed";
        data["old-geometry"] = wf::ipc::geometry_to_json(ev->old_geometry);
        data["view"] = ipc_rules::view_to_json(ev->view);
        send_event_to_subscribes(data, data["event"]);
    };

    wf::signal::connection_t<wf::view_moved_to_wset_signal> on_view_moved_to_wset =
        [=] (wf::view_moved_to_wset_signal *ev)
    {
        wf::json_t data;
        data["event"]    = "view-wset-changed";
        data["old-wset"] = ipc_rules::wset_to_json(ev->old_wset.get());
        data["new-wset"] = ipc_rules::wset_to_json(ev->new_wset.get());
        data["view"]     = ipc_rules::view_to_json(ev->view);
        send_event_to_subscribes(data, data["event"]);
    };

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_kbfocus_changed =
        [=] (wf::keyboard_focus_changed_signal *ev)
    {
        send_view_to_subscribes(wf::node_to_view(ev->new_focus), "view-focused");
    };

    // Tiled rule handler.
    wf::signal::connection_t<wf::view_tiled_signal> _tiled = [=] (wf::view_tiled_signal *ev)
    {
        wf::json_t data;
        data["event"]     = "view-tiled";
        data["old-edges"] = ev->old_edges;
        data["new-edges"] = ev->new_edges;
        data["view"] = ipc_rules::view_to_json(ev->view);
        send_event_to_subscribes(data, data["event"]);
    };

    // Minimized rule handler.
    wf::signal::connection_t<wf::view_minimized_signal> _minimized = [=] (wf::view_minimized_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-minimized");
    };

    // Fullscreened rule handler.
    wf::signal::connection_t<wf::view_fullscreen_signal> _fullscreened = [=] (wf::view_fullscreen_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-fullscreen");
    };

    // Stickied rule handler.
    wf::signal::connection_t<wf::view_set_sticky_signal> _stickied = [=] (wf::view_set_sticky_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-sticky");
    };

    wf::signal::connection_t<wf::view_change_workspace_signal> _view_workspace =
        [=] (wf::view_change_workspace_signal *ev)
    {
        wf::json_t data;
        data["event"] = "view-workspace-changed";
        data["from"]  = wf::ipc::point_to_json(ev->from);
        data["to"]    = wf::ipc::point_to_json(ev->to);
        data["view"]  = ipc_rules::view_to_json(ev->view);
        send_event_to_subscribes(data, data["event"]);
    };

    wf::signal::connection_t<wf::view_title_changed_signal> on_title_changed =
        [=] (wf::view_title_changed_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-title-changed");
    };

    wf::signal::connection_t<wf::view_app_id_changed_signal> on_app_id_changed =
        [=] (wf::view_app_id_changed_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-app-id-changed");
    };

    wf::signal::connection_t<wf::output_plugin_activated_changed_signal> on_plugin_activation_changed =
        [=] (wf::output_plugin_activated_changed_signal *ev)
    {
        wf::json_t data;
        data["event"]  = "plugin-activation-state-changed";
        data["plugin"] = ev->plugin_name;
        data["state"]  = ev->activated;
        data["output"] = ev->output ? (int)ev->output->get_id() : -1;
        data["output-data"] = ipc_rules::output_to_json(ev->output);
        send_event_to_subscribes(data, data["event"]);
    };

    wf::signal::connection_t<wf::output_gain_focus_signal> on_output_gain_focus =
        [=] (wf::output_gain_focus_signal *ev)
    {
        wf::json_t data;
        data["event"]  = "output-gain-focus";
        data["output"] = ipc_rules::output_to_json(ev->output);
        send_event_to_subscribes(data, data["event"]);
    };

    wf::signal::connection_t<wf::input_event_signal<mwlr_keyboard_modifiers_event>> on_keyboard_modifiers =
        [=] (wf::input_event_signal<mwlr_keyboard_modifiers_event> *ev)
    {
        auto seat     = wf::get_core().get_current_seat();
        auto keyboard = wlr_seat_get_keyboard(seat);
        if (ev->device != &keyboard->base)
        {
            return;
        }

        wf::json_t data;
        data["event"] = "keyboard-modifier-state-changed";
        data["state"] = ipc_rules::get_keyboard_state(keyboard);
        send_event_to_subscribes(data, data["event"]);
    };

    wf::signal::connection_t<wf::workspace_set_changed_signal> on_wset_changed =
        [=] (wf::workspace_set_changed_signal *ev)
    {
        wf::json_t data;
        data["event"]    = "output-wset-changed";
        data["new-wset"] = ev->new_wset ? (int)ev->new_wset->get_id() : -1;
        data["output"]   = ev->output ? (int)ev->output->get_id() : -1;
        data["new-wset-data"] = ipc_rules::wset_to_json(ev->new_wset.get());
        data["output-data"]   = ipc_rules::output_to_json(ev->output);
        send_event_to_subscribes(data, data["event"]);
    };

    wf::signal::connection_t<wf::workspace_changed_signal> on_wset_workspace_changed =
        [=] (wf::workspace_changed_signal *ev)
    {
        wf::json_t data;
        data["event"] = "wset-workspace-changed";
        data["previous-workspace"] = wf::ipc::point_to_json(ev->old_viewport);
        data["new-workspace"] = wf::ipc::point_to_json(ev->new_viewport);
        data["output"] = ev->output ? (int)ev->output->get_id() : -1;
        data["wset"]   = (ev->output && ev->output->wset()) ? (int)ev->output->wset()->get_id() : -1;
        data["output-data"] = ipc_rules::output_to_json(ev->output);
        data["wset-data"]   =
            ev->output ? ipc_rules::wset_to_json(ev->output->wset().get()) : json_t::null();
        send_event_to_subscribes(data, data["event"]);
    };

    class ipc_delay_object_t : public txn::transaction_object_t
    {
      public:
        ipc_delay_object_t(std::string name) : obj_name(name)
        {}

        std::string stringify() const override
        {
            return obj_name;
        }

        void commit() override
        {
            if (delay_count < 1)
            {
                wf::txn::emit_object_ready(this);
            }
        }

        void apply() override
        {
            delay_count = 0;
        }

        void set_delay(int count)
        {
            delay_count = count;
        }

        void reduce_delay()
        {
            delay_count--;
            if (delay_count == 0)
            {
                wf::txn::emit_object_ready(this);
            }
        }

        bool currently_blocking() const
        {
            return delay_count > 0;
        }

      private:
        std::string obj_name;
        int delay_count = 0;
    };

    using ipc_delay_object_sptr = std::shared_ptr<ipc_delay_object_t>;
    struct ipc_delay_custom_data_t : public wf::custom_data_t
    {
        ipc_delay_object_sptr delay_obj;
    };

    wf::signal::connection_t<wf::txn::new_transaction_signal> on_new_tx =
        [=] (wf::txn::new_transaction_signal *ev)
    {
        std::vector<wayfire_toplevel_view> mapping_views;
        for (auto& obj : ev->tx->get_objects())
        {
            if (auto toplevel = std::dynamic_pointer_cast<wf::toplevel_t>(obj))
            {
                if (!toplevel->current().mapped && toplevel->pending().mapped)
                {
                    mapping_views.push_back(wf::toplevel_cast(wf::find_view_for_toplevel(toplevel)));
                    wf::dassert(mapping_views.back() != nullptr,
                        "Mapping a toplevel means there must be a corresponding view!");
                }
            }
        }

        for (auto& view : mapping_views)
        {
            if (!view->has_data<ipc_delay_custom_data_t>())
            {
                auto delay_data = std::make_unique<ipc_delay_custom_data_t>();
                delay_data->delay_obj = std::make_shared<ipc_delay_object_t>(
                    "ipc-delay-object-for-view-" + std::to_string(view->get_id()));
                view->store_data<ipc_delay_custom_data_t>(std::move(delay_data));
            }

            auto delay_obj = view->get_data<ipc_delay_custom_data_t>()->delay_obj;
            if (delay_obj->currently_blocking())
            {
                // Already blocked for mapping.
                // This can happen if we have another transaction while the map is being blocked (for example
                // IPC client adjusts the geometry of the view, we shouldn't block again).
                continue;
            }

            // View is about to be mapped. We should notify the IPC clients which care about this
            // that a view is ready for mapping.
            send_view_to_subscribes(view, PRE_MAP_EVENT);

            // Aside from sending a notification, we artificially delay the mapping of the view until
            // the IPC clients notify us that they are ready with the view setup.
            // Detail: we first commit the delay obj on its own. This should be a new tx which
            // goes immediately to commit.
            const int connected_count = signal_map[PRE_MAP_EVENT].connected_count;
            delay_obj->set_delay(connected_count);
            wf::get_core().tx_manager->schedule_object(delay_obj);

            // Next, we add the delay obj to the current transaction, so that the toplevel map is
            // delayed until the delay obj is ready (and it becomes ready when all IPC clients say
            // they have set up the view).
            ev->tx->add_object(delay_obj);
        }
    };

    wf::ipc::method_callback_full on_client_unblock_map =
        [=] (wf::json_t data, wf::ipc::client_interface_t *client)
    {
        auto view = wf::ipc::json_find_view_or_throw(data);
        if (!view->has_data<ipc_delay_custom_data_t>())
        {
            return wf::ipc::json_error("View with id " + std::to_string(view->get_id()) +
                " is not being delayed for mapping");
        }

        auto delay_obj = view->get_data<ipc_delay_custom_data_t>()->delay_obj;
        delay_obj->reduce_delay();
        return wf::ipc::json_ok();
    };
};
}
