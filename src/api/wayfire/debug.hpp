#ifndef DEBUG_HPP
#define DEBUG_HPP

#ifndef WAYFIRE_PLUGIN
    #include "config.h"
#endif

#define nonull(x) ((x) ? (x) : ("nil"))
#include <wayfire/dassert.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/core.hpp>
#include <bitset>

namespace wf
{
/**
 * Dump a scenegraph to the log.
 */
void dump_scene(scene::node_ptr root = wf::get_core().scene());

/**
 * Information about the version that Wayfire was built with.
 * Made available at runtime.
 */
namespace version
{
extern std::string git_commit;
extern std::string git_branch;
}

namespace log
{
/**
 * A list of available logging categories.
 * Logging categories need to be manually enabled.
 */
enum class logging_category : size_t
{
    // Transactions - general
    TXN     = 0,
    // Transactions - individual objects
    TXNI    = 1,
    // Views - events
    VIEWS   = 2,
    // Wlroots messages
    WLR     = 3,
    // Direct scanout
    SCANOUT = 4,
    // Pointer events
    POINTER = 5,
    // Workspace set events
    WSET    = 6,
    // Keyboard-related events
    KBD     = 7,
    // Xwayland-related events
    XWL     = 8,
    // Layer-shell-related events
    LSHELL  = 9,
    // Input-Method-related events
    IM      = 10,
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

std::ostream& operator <<(std::ostream& out, const glm::mat4& mat);
wf::pointf_t operator *(const glm::mat4& m, const wf::pointf_t& p);
wf::pointf_t operator *(const glm::mat4& m, const wf::point_t& p);

namespace wf
{
class view_interface_t;
}

using wayfire_view = nonstd::observer_ptr<wf::view_interface_t>;

namespace wf
{
std::ostream& operator <<(std::ostream& out, wayfire_view view);
}

#endif
