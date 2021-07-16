#pragma once

#include <wayfire/config/option.hpp>
#include <wayfire/view.hpp>

namespace wf
{
/**
 * view_matcher_t provides a way to match certain views based on conditions.
 * The conditions are represented as string options.
 *
 * For information about the syntax or the possible conditions, see
 * wf::view_condition_interface_t and wf::condition_parser_t.
 */
class view_matcher_t
{
  public:
    /**
     * Create a new matcher from the given option.
     *
     * Whenever the option value changes, the condition will be updated.
     *
     * @param option The option where the condition is encoded.
     */
    view_matcher_t(std::shared_ptr<wf::config::option_t<std::string>> option);

    /**
     * Create a new matcher from the given option name.
     * The option will be loaded from core.
     *
     * @throws a std::runtime_error if the option is not found, or if the option
     *   is not a string.
     */
    view_matcher_t(const std::string& option_name);

    view_matcher_t(const view_matcher_t &) = delete;
    view_matcher_t(view_matcher_t &&) = default;
    view_matcher_t& operator =(const view_matcher_t&) = delete;
    view_matcher_t& operator =(view_matcher_t&&) = default;

    /**
     * Set the condition option after initialization.
     */
    void set_from_option(std::shared_ptr<wf::config::option_t<std::string>> option);

    /**
     * @return True if the view matches the condition specified, false otherwise.
     */
    bool matches(wayfire_view view);

    /** Destructor */
    ~view_matcher_t();

  private:
    view_matcher_t();

    class impl;
    std::unique_ptr<impl> priv;
};
}
