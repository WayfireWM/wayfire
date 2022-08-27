#ifndef DEBUG_HPP
#define DEBUG_HPP

#ifndef WAYFIRE_PLUGIN
    #include "config.h"
#endif

#define nonull(x) ((x) ? (x) : ("nil"))
#include <wayfire/util/log.hpp>
#include <wayfire/scene.hpp>
#include <bitset>

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
 * Dump a scenegraph to the log.
 */
void dump_scene(scene::node_ptr root);

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

/* ------------------------ Logging categories -------------------------------*/

namespace wf
{
namespace log
{
/**
 * A list of available logging categories.
 * Logging categories need to be manually enabled.
 */
enum class logging_category : size_t
{
    // Transactions - general
    TXN  = 0,
    // Transactions - view instructions
    TXNV = 1,
    // Transactions - instructions lifetime (pending, ready, timeout, etc.)
    TXNI = 2,
    // Wlroots messages
    WLR  = 3,
    TOTAL,
};

extern std::bitset<(size_t)logging_category::TOTAL> enabled_categories;
}
}

#define LOGC(CAT, ...) \
    if (wf::log::enabled_categories[(size_t)wf::log::logging_category::CAT]) \
    { \
        LOGD("[", #CAT, "] ", __VA_ARGS__); \
    }

/* ------------------- Miscallaneous helpers for debugging ------------------ */
#include <ostream>
#include <glm/glm.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/view.hpp>

std::ostream& operator <<(std::ostream& out, const glm::mat4& mat);
wf::pointf_t operator *(const glm::mat4& m, const wf::pointf_t& p);
wf::pointf_t operator *(const glm::mat4& m, const wf::point_t& p);

namespace wf
{
std::ostream& operator <<(std::ostream& out, wayfire_view view);
}

#endif
