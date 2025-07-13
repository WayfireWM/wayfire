#ifndef DEBUG_HPP
#define DEBUG_HPP

// WF_USE_CONFIG_H is set only when building Wayfire itself, external plugins
// need to use <wayfire/config.h>
#ifdef WF_USE_CONFIG_H
    #include <config.h>
#else
    #include <wayfire/config.h>
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
    TXN           = 0,
    // Transactions - individual objects
    TXNI          = 1,
    // Views - events
    VIEWS         = 2,
    // Wlroots messages
    WLR           = 3,
    // Direct scanout
    SCANOUT       = 4,
    // Pointer events
    POINTER       = 5,
    // Workspace set events
    WSET          = 6,
    // Keyboard-related events
    KBD           = 7,
    // Xwayland-related events
    XWL           = 8,
    // Layer-shell-related events
    LSHELL        = 9,
    // Input-Method-related events
    IM            = 10,
    // Rendering-related events
    RENDER        = 11,
    // Input-device-related events
    INPUT_DEVICES = 12,
    // Output-device-related events
    OUTPUT        = 13,
    // Perfetto events
    PERFETTO      = 14,
    TOTAL,
};

extern std::bitset<(size_t)logging_category::TOTAL> enabled_categories;
}

#define LOGC_ENABLED(CAT) wf::log::enabled_categories[(size_t)wf::log::logging_category::CAT]
#define LOGC(CAT, ...) \
    if (LOGC_ENABLED(CAT)) \
    { \
        LOGD("[", #CAT, "] ", __VA_ARGS__); \
    }

// A wrapper around the perfetto API.
// The functions have dummy implementations if Wayfire is not built with perfetto.
namespace perf
{
enum class category
{
    RENDER = 0,
    PLUGIN = 1,
    TOTAL,
};

struct event_params_t
{
    category cat;
    std::string_view name;
    std::optional<uint64_t> flow_id{};
    std::optional<uint64_t> track_id{};
};

struct dummy_event_t
{};

struct event_t
{
    event_t(const event_params_t& params)
    {
        if (LOGC_ENABLED(PERFETTO))
        {
            start_event(params);
            this->cat = params.cat;
            this->track_id = params.track_id;
        }
    }

    ~event_t()
    {
        if (LOGC_ENABLED(PERFETTO))
        {
            end_event(cat, track_id);
        }
    }

    event_t(const event_t&) = delete;
    event_t(event_t&&) = delete;
    event_t& operator =(const event_t&) = delete;
    event_t& operator =(event_t&&) = delete;

    static void start_event(const event_params_t& params);
    static void end_event(category cat, std::optional<uint64_t> track_id = std::nullopt);

  private:
    category cat = category::TOTAL;
    std::optional<uint64_t> track_id{};
};

uint64_t get_new_track(std::string_view name);
uint64_t get_new_flow();

namespace detail
{
void set_counter(category cat, std::string_view counter_name, int64_t value);
void set_counter(category cat, std::string_view counter_name, double value);
}

inline void set_counter(category cat, std::string_view counter_name, int64_t value)
{
    if (LOGC_ENABLED(PERFETTO))
    {
        detail::set_counter(cat, counter_name, value);
    }
}

inline void set_counter(category cat, std::string_view counter_name, double value)
{
    if (LOGC_ENABLED(PERFETTO))
    {
        detail::set_counter(cat, counter_name, value);
    }
}

void init_perfetto();
void fini_perfetto();
}
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
