#pragma once

#include <stdexcept>
#include <wayfire/config/option.hpp>
#include <wayfire/config/section.hpp>
#include <wayfire/config/compound-option.hpp>

namespace wf
{
template<class T>
using option_sptr_t = std::shared_ptr<wf::config::option_t<T>>;

/**
 * Create an option which has a static value.
 */
template<class T>
option_sptr_t<T> create_option(T value)
{
    return std::make_shared<wf::config::option_t<T>>("Static", value);
}

/**
 * Create an option which has a static value,
 * from the given string description.
 */
template<class T>
option_sptr_t<T> create_option_string(const std::string& value)
{
    return std::make_shared<wf::config::option_t<T>>("Static",
        wf::option_type::from_string<T>(value).value());
}

/**
 * A helper to check whether the given type is a specialization of std::vector
 */
template<class>
struct is_std_vector : public std::false_type {};

template<class V, class A>
struct is_std_vector<std::vector<V, A>> : public std::true_type {};

template<class... Args>
void get_value_from_compound_option(
    config::compound_option_t *opt, config::compound_list_t<Args...>& list)
{
    list = opt->get_value<Args...>();
}

/**
 * A simple wrapper around a config option.
 *
 * This is a base class. Each application which uses it needs to subclass it
 * and override the load_raw_option() method.
 */
template<class Type>
class base_option_wrapper_t
{
  public:
    using OptionType = std::conditional_t<
        is_std_vector<Type>::value,
        config::compound_option_t,
        config::option_t<Type>>;

  public:
    base_option_wrapper_t(const base_option_wrapper_t& other) = delete;
    base_option_wrapper_t& operator =(
        const base_option_wrapper_t& other) = delete;

    base_option_wrapper_t(base_option_wrapper_t&& other) = delete;
    base_option_wrapper_t& operator =(
        base_option_wrapper_t&& other) = delete;

    /**
     * Load the option with the given name from the config.
     * @throws logic_error if the option wrapper already has an option loaded.
     * @throws runtime_error if the given option does not exist or does not
     *   match the type of the option wrapper.
     */
    void load_option(const std::string& name)
    {
        _load_option(load_raw_option(name), name);
    }

    void load_option(std::shared_ptr<wf::config::section_t> section, const std::string& name)
    {
        _load_option(section->get_option_or(name), section->get_name() + "/" + name);
    }

    virtual ~base_option_wrapper_t()
    {
        if (raw_option)
        {
            raw_option->rem_updated_handler(&option_update_listener);
        }
    }

    /** Implicitly convertible to the value of the option */
    operator decltype(auto)() const WF_LIFETIMEBOUND
    {
        return this->value();
    }

    decltype(auto) value() const WF_LIFETIMEBOUND
    {
        if constexpr (is_std_vector<Type>::value)
        {
            Type list;
            get_value_from_compound_option(this->raw_option.get(), list);
            return list;
        } else
        {
            return raw_option->get_value();
        }
    }

    operator std::shared_ptr<OptionType>() const
    {
        return raw_option;
    }

    /** Set a callback to execute when the option value changes. */
    void set_callback(std::function<void()> callback)
    {
        this->on_update = callback;
    }

  protected:
    std::function<void()> on_update;
    wf::config::option_base_t::updated_callback_t option_update_listener;

    /** The actual option wrapped by the option wrapper */
    std::shared_ptr<OptionType> raw_option;

    /**
     * Initialize the option wrapper.
     */
    base_option_wrapper_t()
    {
        option_update_listener = [=] ()
        {
            if (this->on_update)
            {
                this->on_update();
            }
        };
    }

    /**
     * Load the option with the given name from the application configuration.
     */
    virtual std::shared_ptr<wf::config::option_base_t> load_raw_option(
        const std::string& name) = 0;

    void _load_option(std::shared_ptr<wf::config::option_base_t> _option, const std::string& name)
    {
        if (raw_option)
        {
            throw std::logic_error(
                "Loading an option into option wrapper twice!");
        }

        if (_option == nullptr)
        {
            throw std::runtime_error("No such option: " + std::string(name));
        }

        raw_option = std::dynamic_pointer_cast<OptionType>(_option);
        if (raw_option == nullptr)
        {
            throw std::runtime_error("Bad option type: " + name);
        }

        raw_option->add_updated_handler(&option_update_listener);
    }
};
}
