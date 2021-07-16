#pragma once

#include <wayfire/config/option.hpp>
#include <wayfire/config/option-wrapper.hpp>
#include <wayfire/core.hpp>

namespace wf
{
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

    option_wrapper_t() : wf::base_option_wrapper_t<Type>()
    {}

  protected:
    std::shared_ptr<config::option_base_t> load_raw_option(const std::string& name)
    {
        return wf::get_core().config.get_option(name);
    }
};
}
