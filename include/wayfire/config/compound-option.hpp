#pragma once

#include <string>
#include <wayfire/config/types.hpp>
#include <wayfire/config/option.hpp>
#include <wayfire/config/option-types.hpp>
#include <wayfire/util/log.hpp>
#include <vector>
#include <map>
#include <optional>
#include <cassert>

namespace wf
{
namespace config
{
template<class... Args>
using compound_list_t =
    std::vector<std::tuple<std::string, Args...>>;

template<class... Args>
using simple_list_t =
    std::vector<std::tuple<Args...>>;

template<typename T1, typename... Ts>
std::tuple<Ts...> pop_first(const std::tuple<T1, Ts...>& tuple)
{
    return std::apply([] (auto&&, const auto&... args)
    {
        return std::tie(args...);
    }, tuple);
}

template<typename T1, typename... Ts>
std::tuple<T1, Ts...> push_first(const T1& t, const std::tuple<Ts...>& tuple)
{
    return std::tuple_cat(std::tie(t), tuple);
}

/**
 * A base class containing information about an entry in a tuple.
 */
class compound_option_entry_base_t
{
  public:

    virtual ~compound_option_entry_base_t() = default;

    /** @return The prefix of the tuple entry. */
    const std::string& get_prefix() const WF_LIFETIMEBOUND
    {
        return prefix;
    }

    /** @return The name of the tuple entry. */
    const std::string& get_name() const WF_LIFETIMEBOUND
    {
        return name;
    }

    /** @return The untyped default value of the tuple entry. */
    const std::optional<std::string>& get_default_value() const WF_LIFETIMEBOUND
    {
        return default_value;
    }

    /**
     * Try to parse the given value.
     *
     * @param update Whether to override the stored value.
     */
    virtual bool is_parsable(const std::string&) const = 0;

    /** Clone this entry */
    virtual compound_option_entry_base_t *clone() const = 0;

  protected:
    compound_option_entry_base_t() = default;
    std::string prefix;
    std::string name;
    std::optional<std::string> default_value;
};

template<class Type>
class compound_option_entry_t : public compound_option_entry_base_t
{
  public:
    compound_option_entry_t(const std::string& prefix, const std::string& name = "",
        const std::optional<std::string>& default_value = std::nullopt)
    {
        this->prefix = prefix;
        this->name   = name;
        this->default_value = default_value;
        assert(!this->default_value || is_parsable(this->default_value.value()));
    }

    compound_option_entry_base_t *clone() const override
    {
        return new compound_option_entry_t<Type>(*this);
    }

    /**
     * Try to parse the given value.
     *
     * @param update Whether to override the stored value.
     */
    bool is_parsable(const std::string& str) const override
    {
        return option_type::from_string<Type>(str).has_value();
    }
};

/**
 * Compound options are a special class of options which can hold multiple
 * string-tagged tuples. They are constructed from multiple untyped options
 * in the config file.
 */

class compound_option_t : public option_base_t
{
  public:
    using entries_t = std::vector<std::unique_ptr<compound_option_entry_base_t>>;
    /**
     * Construct a new compound option, with the types given in the template
     * arguments.
     *
     * @param name The name of the option.
     * @param prefixes The prefixes used for grouping in the config file.
     *   Example: Consider a compound option with type <int, double> and two
     *   prefixes {"prefix1_", "prefix2_"}. In the config file, the options are:
     *
     *   prefix1_key1 = v11
     *   prefix2_key1 = v21
     *   prefix1_key2 = v12
     *   prefix2_key2 = v22
     *
     *   Options are grouped by suffixes (key1 and key2), and the tuples then
     *   are formed by taking the values of the options with each prefix.
     *   So, the tuples contained in the compound option in the end are:
     *
     *   (key1, v11, v21)
     *   (key2, v12, v22)
     * @param type_hint What type of dynamic-list is this option. This stores
     *    the type-hint attribute of the option specified in the xml. Currently,
     *    the valid types are plain, dict and tuple (default). Type-hint is
     *    mainly used as hint to save the config option in a "pretty" way.
     *    Config formats are free to ignore this type hint.
     */
    compound_option_t(const std::string& name, entries_t&& entries,
        std::string type_hint = "tuple");

    /**
     * Parse the compound option with the given types.
     *
     * Throws an exception in case of wrong template types.
     */
    template<class... Args>
    compound_list_t<Args...> get_value() const
    {
        compound_list_t<Args...> result;
        result.resize(value.size());
        build_recursive<0, Args...>(result);
        return result;
    }

    template<class... Args>
    simple_list_t<Args...> get_value_simple() const
    {
        auto list = get_value<Args...>();
        simple_list_t<Args...> result;
        for (const auto& val : list)
        {
            result.push_back(pop_first(val));
        }

        return result;
    }

    /**
     * Set the value of the option.
     *
     * Throws an exception in case of wrong template types.
     */
    template<class... Args>
    void set_value(const compound_list_t<Args...>& value)
    {
        assert(sizeof...(Args) == this->entries.size());
        this->value.assign(value.size(), {});
        push_recursive<0>(value);
        notify_updated();
    }

    /**
     * Set the value of the option.
     *
     * Throws an exception in case of wrong template types.
     */
    template<class... Args>
    void set_value_simple(const simple_list_t<Args...>& value)
    {
        compound_list_t<Args...> list;
        for (int i = 0; i < (int)value.size(); i++)
        {
            list.push_back(push_first(std::to_string(i), value[i]));
        }

        this->set_value(list);
    }

    using stored_type_t = std::vector<std::vector<std::string>>;
    /**
     * Get the string data stored in the compound option.
     */
    stored_type_t get_value_untyped();

    /**
     * Set the data contained in the option, from a vector containing
     * strings which describe the individual elements.
     *
     * @return True if the operation was successful.
     */
    bool set_value_untyped(stored_type_t value);

    /**
     * Get the type information about entries in the option.
     */
    const entries_t& get_entries() const WF_LIFETIMEBOUND;

    /**
     * Check if this compound option has named tuples.
     */
    const std::string& get_type_hint() const WF_LIFETIMEBOUND
    {
        return list_type_hint;
    }

  private:
    /**
     * Current value stored in the option.
     * The first element is the name of the tuple, followed by the string values
     * of each element.
     */
    stored_type_t value;

    /** Entry types with which the option was created. */
    entries_t entries;

    /** What type of dynamic-list is this: plain, dics, tuple */
    std::string list_type_hint;

    /**
     * Set the n-th element in the result tuples by reading from the stored
     * values in this option.
     */
    template<size_t n, class... Args>
    void build_recursive(compound_list_t<Args...>& result) const
    {
        for (size_t i = 0; i < result.size(); i++)
        {
            using type_t = typename std::tuple_element<n,
                std::tuple<std::string, Args...>>::type;

            std::get<n>(result[i]) = option_type::from_string<type_t>(
                this->value[i][n]).value();
        }

        // Recursively build the (N+1)'th entries
        if constexpr (n < sizeof...(Args))
        {
            build_recursive<n + 1>(result);
        }
    }

    template<size_t n, class... Args>
    void push_recursive(const compound_list_t<Args...>& new_value)
    {
        for (size_t i = 0; i < new_value.size(); i++)
        {
            using type_t = typename std::tuple_element<n,
                std::tuple<std::string, Args...>>::type;

            this->value[i].push_back(option_type::to_string<type_t>(
                std::get<n>(new_value[i])));
        }

        // Recursively build the (N+1)'th entries
        if constexpr (n < sizeof...(Args))
        {
            push_recursive<n + 1>(new_value);
        }
    }

  public: // Implementation of option_base_t
    std::shared_ptr<option_base_t> clone_option() const override;
    bool set_value_str(const std::string&) override;
    void reset_to_default() override;
    bool set_default_value_str(const std::string&) override;
    std::string get_value_str() const override;
    std::string get_default_value_str() const override;
};
}
}
