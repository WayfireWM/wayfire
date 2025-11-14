#pragma once

#include <wayfire/config/option-types.hpp>
#include <functional>
#include <limits>

#include <memory>

#ifdef __clang__
    #define WF_LIFETIMEBOUND [[clang::lifetimebound]]
#else
    #define WF_LIFETIMEBOUND
#endif

namespace wf
{
namespace config
{
/**
 * A base class for all option types.
 */
class option_base_t
{
  public:
    virtual ~option_base_t();
    option_base_t(const option_base_t& other) = delete;
    option_base_t& operator =(const option_base_t& other) = delete;

    /** @return The name of the option */
    const std::string& get_name() const WF_LIFETIMEBOUND;

    /** @return A copy of the option */
    virtual std::shared_ptr<option_base_t> clone_option() const = 0;

    /**
     * Set the option value from the given string.
     * Invalid values are ignored.
     *
     * @return true if the option value was updated.
     */
    virtual bool set_value_str(const std::string& value) = 0;

    /** Reset the option to its default value.  */
    virtual void reset_to_default() = 0;

    /**
     * Change the default value of an option. Note that this will not change the
     * option value, only its default value.
     *
     * If the new default value is invalid, the request will be ignored.
     * @return true if the default value was updated.
     */
    virtual bool set_default_value_str(const std::string& default_value) = 0;

    /** Get the option value in string format */
    virtual std::string get_value_str() const = 0;

    /** Get the default option value in string format */
    virtual std::string get_default_value_str() const = 0;

    /**
     * A function to be executed when the option value changes.
     */
    using updated_callback_t = std::function<void ()>;

    /**
     * Register a new callback to execute when the option value changes.
     */
    void add_updated_handler(updated_callback_t *callback);

    /**
     * Unregister a callback to execute when the option value changes.
     * If the same callback has been registered multiple times, this unregister
     * all registered instances.
     */
    void rem_updated_handler(updated_callback_t *callback);

    /**
     * Set the lock status of an option, this is reference-counted.
     *
     * An option is unlocked by default. When an option is locked, the option
     * should not be modified by any config backend (for ex. when reading from
     * a file).
     *
     * Note that changing the value of the option manually still works.
     */
    void set_locked(bool locked = true);

    /**
     * Get the current locked status.
     */
    bool is_locked() const;

    struct impl;
    std::unique_ptr<impl> priv;

  protected:
    /** Construct a new option with the given name. */
    option_base_t(const std::string& name);

    /** Notify all watchers */
    void notify_updated() const;

    /** Initialize a cloned version of this option. */
    void init_clone(option_base_t& clone) const;
};

/**
 * A base class for options which can have minimum and maximum.
 * By default, no bounding checks are enabled.
 */
template<class Type, bool enable_bounds>
class bounded_option_base_t
{
  protected:
    Type closest_valid_value(const Type& value) const
    {
        return value;
    }
};

/**
 * Specialization for option types which do support bounded values.
 */
template<class Type>
class bounded_option_base_t<Type, true>
{
  public:
    /** @return The minimal permissible value for this option, if it is set. */
    std::optional<Type> get_minimum() const
    {
        return minimum;
    }

    /** @return The maximal permissible value for this option, if it is set. */
    std::optional<Type> get_maximum() const
    {
        return maximum;
    }

  protected:
    std::optional<Type> minimum;
    std::optional<Type> maximum;

    /**
     * @return The closest possible value
     */
    Type closest_valid_value(const Type& value) const
    {
        auto real_minimum =
            minimum.value_or(std::numeric_limits<Type>::lowest());
        auto real_maximum =
            maximum.value_or(std::numeric_limits<Type>::max());

        if (value < real_minimum)
        {
            return real_minimum;
        }

        if (value > real_maximum)
        {
            return real_maximum;
        }

        return value;
    }
};

namespace detail
{
template<class Type, class Result> using boundable_type_only =
    std::enable_if_t<std::is_arithmetic<Type>::value, Result>;
}

/**
 * Represents an option of the given type.
 */
template<class Type>
class option_t : public option_base_t,
    public bounded_option_base_t<Type, std::is_arithmetic<Type>::value>
{
  public:
    /**
     * Create a new option with the given name and default value.
     */
    option_t(const std::string& name, Type def_value) :
        option_base_t(name), default_value(def_value), value(default_value)
    {}

    /**
     * Create a copy of the option.
     */
    virtual std::shared_ptr<option_base_t> clone_option() const override
    {
        auto result = std::make_shared<option_t>(get_name(), get_default_value());
        result->set_value(get_value());
        if constexpr (std::is_arithmetic<Type>::value)
        {
            result->minimum = this->minimum;
            result->maximum = this->maximum;
        }

        init_clone(*result);
        return result;
    }

    /**
     * Set the value of the option from the given string.
     * The value will be auto-clamped to the defined bounds, if they exist.
     * If the value actually changes, the updated handlers will be called.
     */
    virtual bool set_value_str(const std::string& new_value_str) override
    {
        auto new_value = option_type::from_string<Type>(new_value_str);
        if (new_value)
        {
            set_value(new_value.value());
            return true;
        }

        return false;
    }

    /**
     * Reset the option to its default value.
     */
    virtual void reset_to_default() override
    {
        set_value(default_value);
    }

    /**
     * Change the default value of the function, if possible.
     */
    virtual bool set_default_value_str(const std::string& defvalue) override
    {
        auto parsed = option_type::from_string<Type>(defvalue);
        if (parsed)
        {
            this->default_value = parsed.value();
            return true;
        }

        return false;
    }

    /**
     * Set the value of the option.
     * The value will be auto-clamped to the defined bounds, if they exist.
     * If the value actually changes, the updated handlers will be called.
     */
    void set_value(const Type& new_value)
    {
        auto real_value = this->closest_valid_value(new_value);
        if (!(this->value == real_value))
        {
            this->value = real_value;
            this->notify_updated();
        }
    }

    const Type& get_value() const WF_LIFETIMEBOUND
    {
        return value;
    }

    const Type& get_default_value() const WF_LIFETIMEBOUND
    {
        return default_value;
    }

    virtual std::string get_value_str() const override
    {
        return option_type::to_string<Type>(get_value());
    }

    virtual std::string get_default_value_str() const override
    {
        return option_type::to_string<Type>(get_default_value());
    }

  public:
    /**
     * Set the minimum permissible value for arithmetic type options.
     * An attempt to set the value to a value below the minimum will set the
     * value of the option to the minimum.
     */
    template<class U = void>
    detail::boundable_type_only<Type, U> set_minimum(Type min)
    {
        this->minimum = {min};
        this->value   = this->closest_valid_value(this->value);
    }

    /**
     * Set the maximum permissible value for arithmetic type options.
     * An attempt to set the value to a value above the maximum will set the
     * value of the option to the maximum.
     */
    template<class U = void>
    detail::boundable_type_only<Type, U> set_maximum(Type max)
    {
        this->maximum = {max};
        this->value   = this->closest_valid_value(this->value);
    }

  protected:
    Type default_value; /* default value */
    Type value; /* current value */
};
}
}
