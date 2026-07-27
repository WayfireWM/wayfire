#include <memory>
#include <vector>

#include <wayfire/per-output-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/view-access-interface.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/parser/rule_parser.hpp>
#include <wayfire/lexer/lexer.hpp>
#include <wayfire/variant.hpp>
#include <wayfire/rule/lambda_rule.hpp>
#include <wayfire/rule/rule.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/txn/transaction-manager.hpp>

#include "lambda-rules-registration.hpp"
#include "view-action-interface.hpp"
#include "wayfire/signal-provider.hpp"

// Marks a view whose "created" rules have been applied while its map transaction was pending.
static const std::string created_rules_applied = "window-rules-created-applied";

class wayfire_window_rules_t : public wf::per_output_plugin_instance_t
{
  public:
    void init() override;
    void fini() override;
    void apply(const std::string & signal, wayfire_view view);

  private:
    void setup_rules_from_config();
    wf::lexer_t _lexer;

    // Created rule handler for views without a toplevel (e.g. popups). They are not part of any
    // transaction, so the rules for them are applied after they have been mapped.
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        if (!toplevel_cast(ev->view))
        {
            apply("created", ev->view);
            return;
        }

        // Allow rules to be applied again when the view is mapped next time.
        ev->view->erase_data(created_rules_applied);
    };

    // Created rule handler for toplevel views. The rules are applied when the map transaction of
    // a view is scheduled, but before it is committed.
    wf::signal::connection_t<wf::txn::new_transaction_signal> on_new_tx =
        [=] (wf::txn::new_transaction_signal *ev)
    {
        for (const auto& obj : ev->tx->get_objects())
        {
            auto toplevel = std::dynamic_pointer_cast<wf::toplevel_t>(obj);
            if (!toplevel || !map_pending(toplevel))
            {
                continue;
            }

            auto view = wf::find_view_for_toplevel(toplevel);
            if (!view || (view->get_output() != output))
            {
                continue;
            }

            // Handler may be invoked several times before the view is mapped, apply the rules only once.
            if (view->has_data(created_rules_applied))
            {
                continue;
            }

            view->store_data(std::make_unique<wf::custom_data_t>(), created_rules_applied);
            apply("created", view);
        }
    };

    bool map_pending(std::shared_ptr<wf::toplevel_t> toplevel)
    {
        return !toplevel->current().mapped && toplevel->pending().mapped;
    }

    // Maximized rule handler.
    wf::signal::connection_t<wf::view_tiled_signal> _tiled = [=] (wf::view_tiled_signal *ev)
    {
        apply("maximized", ev->view);
        apply("unmaximized", ev->view);
    };

    // Minimized rule handler.
    wf::signal::connection_t<wf::view_minimized_signal> _minimized = [=] (wf::view_minimized_signal *ev)
    {
        apply("minimized", ev->view);
    };

    // Fullscreened rule handler.
    wf::signal::connection_t<wf::view_fullscreen_signal> _fullscreened = [=] (wf::view_fullscreen_signal *ev)
    {
        apply("fullscreened", ev->view);
    };

    // Auto-reload on changes to config file
    wf::signal::connection_t<wf::reload_config_signal> _reload_config = [=] (wf::reload_config_signal *ev)
    {
        setup_rules_from_config();
    };

    std::vector<std::shared_ptr<wf::rule_t>> _rules;

    wf::view_access_interface_t _access_interface;
    wf::view_action_interface_t _action_interface;

    nonstd::observer_ptr<wf::lambda_rules_registrations_t> _lambda_registrations;
};

void wayfire_window_rules_t::init()
{
    // Get the lambda rules registrations.
    _lambda_registrations = wf::lambda_rules_registrations_t::get_instance();
    _lambda_registrations->window_rule_instances++;

    setup_rules_from_config();

    output->connect(&on_view_mapped);
    wf::get_core().tx_manager->connect(&on_new_tx);
    output->connect(&_tiled);
    output->connect(&_minimized);
    output->connect(&_fullscreened);
    wf::get_core().connect(&_reload_config);
}

void wayfire_window_rules_t::fini()
{
    _lambda_registrations->window_rule_instances--;
    if (_lambda_registrations->window_rule_instances == 0)
    {
        wf::get_core().erase_data<wf::lambda_rules_registrations_t>();
    }
}

void wayfire_window_rules_t::apply(const std::string & signal, wayfire_view view)
{
    if (view == nullptr)
    {
        return;
    }

    auto toplevel = toplevel_cast(view);
    if ((signal == "maximized") && (!toplevel || (toplevel->pending_tiled_edges() != wf::TILED_EDGES_ALL)))
    {
        return;
    }

    if ((signal == "unmaximized") && (!toplevel || (toplevel->pending_tiled_edges() == wf::TILED_EDGES_ALL)))
    {
        return;
    }

    for (const auto & rule : _rules)
    {
        _access_interface.set_view(view);
        _action_interface.set_view(view);
        auto error = rule->apply(signal, _access_interface, _action_interface);
        if (error)
        {
            LOGE("Window-rules: Error while executing rule on ", signal, " signal.");
        }
    }

    auto bounds = _lambda_registrations->rules();
    auto begin  = std::get<0>(bounds);
    auto end    = std::get<1>(bounds);
    while (begin != end)
    {
        auto registration = std::get<1>(*begin);
        bool error = false;

        // Assume we will use the view access interface.
        _access_interface.set_view(view);
        wf::access_interface_t & access_iface = _access_interface;

        // If a custom access interface is set in the regoistration, use this one.
        if (registration->access_interface != nullptr)
        {
            access_iface = *registration->access_interface;
        }

        // Load if lambda wrapper.
        if (registration->if_lambda != nullptr)
        {
            registration->rule_instance->setIfLambda(
                [registration, signal, view] () -> bool
            {
                return registration->if_lambda(signal, view);
            });
        }

        // Load else lambda wrapper.
        if (registration->else_lambda)
        {
            registration->rule_instance->setElseLambda(
                [registration, signal, view] () -> bool
            {
                return registration->else_lambda(signal, view);
            });
        }

        // Run the lambda rule.
        error = registration->rule_instance->apply(signal, _access_interface);

        // Unload wrappers.
        registration->rule_instance->setIfLambda(nullptr);
        registration->rule_instance->setElseLambda(nullptr);

        if (error)
        {
            LOGE("Window-rules: Error while executing rule on signal: ", signal,
                ", rule text:", registration->rule);
        }

        ++begin;
    }
}

void wayfire_window_rules_t::setup_rules_from_config()
{
    _rules.clear();

    wf::option_wrapper_t<wf::config::compound_list_t<std::string>> rule_list_option{"window-rules/rules"};
    auto rule_list = rule_list_option.value();

    for (const auto& [name, rule_str] : rule_list)
    {
        LOGD("Registering ", rule_str);
        _lexer.reset(rule_str);
        auto rule = wf::rule_parser_t().parse(_lexer);
        if (rule != nullptr)
        {
            _rules.push_back(rule);
        }
    }
}

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_window_rules_t>);
