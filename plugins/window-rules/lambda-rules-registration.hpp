#ifndef LAMBDARULESREGISTRATION_HPP
#define LAMBDARULESREGISTRATION_HPP

#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "wayfire/core.hpp"
#include "wayfire/object.hpp"
#include "wayfire/nonstd/observer_ptr.h"
#include "wayfire/parser/lambda_rule_parser.hpp"
#include "wayfire/rule/lambda_rule.hpp"
#include "wayfire/util/log.hpp"
#include "wayfire/view.hpp"

class wayfire_window_rules_t;

namespace wf
{
struct lambda_rule_registration_t;

using map_type = std::map<std::string, std::shared_ptr<lambda_rule_registration_t>>;

using lambda_reg_t = std::function<bool (std::string, wayfire_view)>;

/**
 * @brief The lambda_rule_registration_t struct represents registration information
 * for a single lambda rule.
 *
 * To make a registration, create one of these structures in a shared_ptr, fill in
 * the appropriate values and
 * register it on the lambda_rules_registrations_t singleton instance.
 *
 * At minimum, the rule string and if_lambda need to be set.
 *
 * The rule text defines the condition that will will be matched by window-rules. If
 * the condition described
 * in the rule text evaluates to true (using access_interface to determine the
 * current values of variables),
 * the if_lambda function will be executed. If the condition evaluates to false, the
 * else_lambda (if not
 * nullptr) will be executed.
 */
struct lambda_rule_registration_t
{
  public:
    /**
     * @brief rule This is the rule text.
     *
     * @note The registering plugin is supposed to set this value before registering.
     */
    std::string rule;

    /**
     * @brief if_lambda This is the lambda method to be executed if the specified
     * condition holds.
     *
     * @note The registering plugin is supposed to set this value before registering.
     */
    wf::lambda_reg_t if_lambda;

    /**
     * @brief else_lambda This is the lambda method to be executed if the specified
     * condition does not hold.
     *
     * @note The registering plugin is supposed to set this value before registering.
     * @note In most cases this should be left blank.
     *
     * @attention: Be very careful with this lambda because it will be executed on
     * the signal for each view
     *             that did NOT match the condition.
     */
    wf::lambda_reg_t else_lambda;

    /**
     * @brief access_interface Access interface to be used when evaluating the rule.
     *
     * @note If this is left blank (nullptr), the standard view_access_interface_t
     * instance will be used.
     */
    std::shared_ptr<wf::access_interface_t> access_interface;

  private:
    /**
     * @brief rule_instance Pointer to the parsed rule object.
     *
     * @attention You should not set this. Leave it at nullptr, the registration
     * process will fill in this
     *            variable. Window rules can then use this cached rule instance on
     * each signal occurrence.
     */
    std::shared_ptr<wf::lambda_rule_t> rule_instance;

    // Friendship for window rules to be able to execute the rules.
    friend class ::wayfire_window_rules_t;

    // Friendship for the rules registrations to be able to modify the rule set.
    friend class lambda_rules_registrations_t;
};

/**
 * @brief The lambda_rules_registrations_t class is a helper class for easy
 * registration and unregistration of
 *        lambda rules for the window rules plugin.
 *
 * This class is a singleton and can only be used via the getInstance() method.
 *
 * The instance is stored in wf::core. The getInstance() method will fetch from
 * wf:core, and lazy-init if the
 * instance is not yet present.
 */
class lambda_rules_registrations_t
{
  public:
    /**
     * @brief getInstance Static accessor for the singleton.
     *
     * @return Observer pointer to the singleton instance, fetched from wf::core.
     */
    static nonstd::observer_ptr<lambda_rules_registrations_t> get_instance()
    {
        auto instance = get_core().get_data<lambda_rules_registrations_t>();
        if (instance == nullptr)
        {
            get_core().store_data(std::unique_ptr<lambda_rules_registrations_t>(
                new lambda_rules_registrations_t()));
            instance = get_core().get_data<lambda_rules_registrations_t>();

            if (instance == nullptr)
            {
                LOGE("Window lambda rules: Lazy-init of lambda registrations failed.");
            } else
            {
                LOGD(
                    "Window lambda rules: Lazy-init of lambda registrations succeeded.");
            }
        }

        return instance;
    }

    /**
     * @brief registerLambdaRule Registers a lambda rule with its associated key.
     *
     * This method will return error result if the key is not unique or the
     * registration struct is incomplete.
     *
     * @param[in] key Unique key for the registration.
     * @param[in] registration The registration structure.
     *
     * @return <code>True</code> in case of error, <code>false</code> if ok.
     */
    bool register_lambda_rule(std::string key,
        std::shared_ptr<lambda_rule_registration_t> registration)
    {
        if (_registrations.find(key) != _registrations.end())
        {
            return true; // Error, key already exists.
        }

        if (registration->if_lambda == nullptr)
        {
            return true; // Error, no if lambda specified.
        }

        registration->rule_instance = lambda_rule_parser_t().parse(
            registration->rule, nullptr, nullptr);
        if (registration->rule_instance == nullptr)
        {
            return true; // Error, failed to parse rule.
        }

        _registrations.emplace(key, registration);

        return false;
    }

    /**
     * @brief unregisterLambdaRule Unregisters a lambda rule with its associated key.
     *
     * Has no effect if no rule is registered with this key.
     *
     * @param[in] key Unique key for the registration.
     */
    void unregister_lambda_rule(std::string key)
    {
        _registrations.erase(key);
    }

    /**
     * @brief rules Gets the boundaries of the rules map as a tuple of cbegin() and
     * cend() const_iterators.
     *
     * @return Boundaries of the rules map.
     */
    std::tuple<map_type::const_iterator, map_type::const_iterator> rules()
    {
        return std::tuple(_registrations.cbegin(), _registrations.cend());
    }

  private:
    /**
     * @brief lambda_rules_registrations_t Constructor, private to enforce singleton
     * design pattern.
     */
    lambda_rules_registrations_t() = default;

    /**
     * @brief _registrations The map holding all the current registrations.
     */
    map_type _registrations;

    // Necessary for window-rules to manage the lifetime of the object
    uint32_t window_rule_instances = 0;
    friend class ::wayfire_window_rules_t;
};
} // End namespace wf.

#endif // LAMBDARULESREGISTRATION_HPP
