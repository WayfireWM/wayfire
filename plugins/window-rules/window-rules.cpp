#include <algorithm>
#include <cfloat>
#include <memory>
#include <vector>

#include <wayfire/plugin.hpp>
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

#include "lambda-rules-registration.hpp"
#include "view-action-interface.hpp"

class wayfire_window_rules_t : public wf::plugin_interface_t
{
  public:
    void init() override;
    void fini() override;
    void apply(const std::string & signal, wf::signal_data_t *data);

  private:
    wf::lexer_t _lexer;

    wf::signal_callback_t _created;
    wf::signal_callback_t _maximized;
    wf::signal_callback_t _unmaximized;
    wf::signal_callback_t _minimized;
    wf::signal_callback_t _fullscreened;

    std::vector<std::shared_ptr<wf::rule_t>> _rules;

    wf::view_access_interface_t _access_interface;
    wf::view_action_interface_t _action_interface;

    nonstd::observer_ptr<wf::lambda_rules_registrations_t> _lambda_registrations;
};

void wayfire_window_rules_t::init()
{
    // Get the lambda rules registrations.
    _lambda_registrations = wf::lambda_rules_registrations_t::get_instance();

    // Build rule list.
    auto section = wf::get_core().config.get_section("window-rules");
    for (auto opt : section->get_registered_options())
    {
        _lexer.reset(opt->get_value_str());
        auto rule = wf::rule_parser_t().parse(_lexer);
        if (rule != nullptr)
        {
            _rules.push_back(rule);
        }
    }

    // Created rule handler.
    _created = [=] (wf::signal_data_t *data)
    {
        apply("created", data);
    };
    output->connect_signal("view-mapped", &_created);

    // Maximized rule handler.
    _maximized = [=] (wf::signal_data_t *data)
    {
        apply("maximized", data);
    };
    output->connect_signal("view-tiled", &_maximized);

    // Unaximized rule handler.
    _unmaximized = [=] (wf::signal_data_t *data)
    {
        apply("unmaximized", data);
    };
    output->connect_signal("view-tiled", &_unmaximized);

    // Minimized rule handler.
    _minimized = [=] (wf::signal_data_t *data)
    {
        apply("minimized", data);
    };
    output->connect_signal("view-minimized", &_minimized);

    // Fullscreened rule handler.
    _fullscreened = [=] (wf::signal_data_t *data)
    {
        apply("fullscreened", data);
    };
    output->connect_signal("view-fullscreen", &_fullscreened);
}

void wayfire_window_rules_t::fini()
{
    output->disconnect_signal("view-mapped", &_created);
    output->disconnect_signal("view-tiled", &_maximized);
    output->disconnect_signal("view-tiled", &_unmaximized);
    output->disconnect_signal("view-minimized", &_minimized);
    output->disconnect_signal("view-fullscreen", &_fullscreened);
}

void wayfire_window_rules_t::apply(const std::string & signal,
    wf::signal_data_t *data)
{
    if (data == nullptr)
    {
        return;
    }

    auto view = get_signaled_view(data);
    if (view == nullptr)
    {
        LOGE("View is null.");

        return;
    }

    if ((signal == "maximized") && (view->tiled_edges != wf::TILED_EDGES_ALL))
    {
        return;
    }

    if ((signal == "unmaximized") && (view->tiled_edges == wf::TILED_EDGES_ALL))
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

DECLARE_WAYFIRE_PLUGIN(wayfire_window_rules_t);
