#pragma once

#include <wayfire/config/option.hpp>
#include <wayfire/config/option-wrapper.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util/duration.hpp>

namespace wf
{
namespace detail
{
// Forward declaration to avoid adding unnecessary includes.
[[noreturn]]
void option_wrapper_debug_message(const std::string& option_name, const std::runtime_error& err);
[[noreturn]]
void option_wrapper_debug_message(const std::string& option_name, const std::logic_error& err);
std::shared_ptr<config::option_base_t> load_raw_option(const std::string& name);
}

/**
 * A simple wrapper around a config option.
 */
template<class Type>
class option_wrapper_t : public base_option_wrapper_t<Type>
{
  public:
    /**
     * Initialize the option wrapper and directly load the given option.
     */
    option_wrapper_t(const std::string& option_name) :
        wf::base_option_wrapper_t<Type>()
    {
        this->load_option(option_name);
    }

    void load_option(const std::string& option_name)
    {
        try {
            base_option_wrapper_t<Type>::load_option(option_name);
        } catch (const std::runtime_error& err)
        {
            detail::option_wrapper_debug_message(option_name, err);
        } catch (const std::logic_error& err)
        {
            detail::option_wrapper_debug_message(option_name, err);
        }
    }

    option_wrapper_t() : wf::base_option_wrapper_t<Type>()
    {}

  protected:
    std::shared_ptr<wf::config::option_base_t> load_raw_option(const std::string& name) override
    {
        return detail::load_raw_option(name);
    }
};
}
