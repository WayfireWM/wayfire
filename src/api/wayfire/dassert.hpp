#pragma once

#include <string>
#include <wayfire/util/log.hpp>

namespace wf
{
/**
 * Print the current stacktrace at runtime.
 *
 * @param fast_mode If fast_mode is true, the stacktrace will be generated
 *   using the fastest possible method. However, this means that not all
 *   information will be printed (for ex., line numbers may be missing).
 */
void print_trace(bool fast_mode);

/**
 * Assert that the condition is true.
 * Optionally print a message.
 * Print backtrace when the assertion fails and exit.
 */
inline void dassert(bool condition, std::string message = "")
{
    if (!condition)
    {
        LOGE(message);
        print_trace(false);
        std::exit(0);
    }
}
}

#define DASSERT(condition) \
    wf::dassert(condition, "Assertion failed at " __FILE__ ":" __LINE__)
