#include <string>
#include <wayfire/config/option-types.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/view.hpp>
#include <wayfire/option-wrapper.hpp>
#include <sstream>
#include <stdlib.h>
#include <iomanip>

#if __has_include(<execinfo.h>)
    #include <execinfo.h>
    #include <cxxabi.h>
#endif

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <sys/stat.h>
#include <iostream>

#define MAX_FRAMES 256
#define MAX_FUNCTION_NAME 1024

#if __has_include(<execinfo.h>)
struct demangling_result
{
    std::string executable;
    std::string function_name;
    std::string address;
};

/**
 * Parse a mangled symbol and offset in the backtrace_symbols format:
 *
 * executable(function+offset) [global offset]
 */
demangling_result demangle_function(std::string symbol)
{
    demangling_result result;
    size_t offset_begin = symbol.find_first_of("[");
    size_t offset_end   = symbol.find_first_of("]");

    if ((offset_begin != std::string::npos) && (offset_end != std::string::npos) &&
        (offset_begin < offset_end))
    {
        offset_begin++;
        result.address = symbol.substr(offset_begin, offset_end - offset_begin);
    }

    size_t function_begin = symbol.find_first_of("(");
    if (function_begin != std::string::npos)
    {
        result.executable = symbol.substr(0, function_begin);
    }

    size_t function_end = symbol.find_first_of("+");
    if ((function_begin == std::string::npos) ||
        (function_end == std::string::npos) ||
        (function_begin >= function_end))
    {
        return result;
    }

    ++function_begin;
    std::string function_name = symbol.substr(function_begin,
        function_end - function_begin);

    int status;
    char *demangled = abi::__cxa_demangle(function_name.data(), NULL, NULL, &status);
    if (status != 0)
    {
        free(demangled);

        return result;
    }

    result.function_name = demangled;
    free(demangled);

    return result;
}

/**
 * Execute the given command and read the first line of its output.
 *
 * Returns an empty string in case of an error.
 */
std::string read_output(std::string command)
{
    // Open a pipe stream to read the output of the process `command`.
    FILE *file = popen(command.c_str(), "r");
    if (!file)
    {
        // popen failed.
        return {};
    }

    // Read the first line, up till and including the first newline if any, into a buffer.
    char buffer[MAX_FUNCTION_NAME + 1]; // +1 for the terminating zero.
    char *line_as_c_str = fgets(buffer, sizeof(buffer), file);

    // We're done with the pipe stream now.
    pclose(file);

    if (!line_as_c_str)
    {
        // fgets returned an error.
        return {};
    }

    // If fgets returns non-NULL then the string is guaranteed to be zero terminated.
    size_t len = std::strlen(line_as_c_str);

    // Strip the trailing newline, if any.
    if ((len > 0) && (line_as_c_str[len - 1] == '\n'))
    {
        --len;
        line_as_c_str[len] = 0;
    }

    return {buffer, len};
}

/**
 * Try to find the correct path to the given executable.
 *
 * If the path is relative(beginning with . or ..), or absolute, we already
 * have the corrent path. Otherwise, try to find it with 'which'
 */
std::string locate_executable(std::string executable)
{
    if (!executable.length())
    {
        return "";
    }

    /* If absolute/relative address, we can't do anything */
    if ((executable[0] == '/') || (executable[0] == '.'))
    {
        return executable;
    }

    /* If just an executable, try to find it in PATH */
    auto path = read_output("which " + executable);

    return path;
}

/**
 * Find the first position where ".." is, and then strip everything before that.
 */
std::string strip_until_dots(std::string line)
{
    size_t pos = line.find("..");
    if (pos != std::string::npos)
    {
        return line.substr(pos);
    }

    return line;
}

/**
 * A hexadecimal offset to void* pointer conversion
 */
void *hex_to_ptr(std::string ptr)
{
    size_t idx;

    return reinterpret_cast<void*>(std::stoul(ptr, &idx, 16));
}

/**
 * Try to run addr2line for the given executable and address.
 */
std::string try_addr2line(std::string executable, std::string address,
    std::string flags = "")
{
    std::string command =
        "addr2line " + flags + " -e " + executable + " " + address;
    auto location = read_output(command);

    return strip_until_dots(location);
}

/**
 * Check whether the addr2line returned a valid position.
 */
bool valid_addr2line_return(std::string output)
{
    return !output.empty() && output[0] != '?';
}

struct addr2line_result
{
    std::string function_name;
    std::string function_source;
};

/**
 * Try to locate the source file for the given address.
 * If addr2line is not available on the system, the operation simply returns
 * an string "unknown location(address)".
 */
addr2line_result locate_source_file(const demangling_result& dr)
{
    auto executable = locate_executable(dr.executable);

    if (executable.empty() || dr.address.empty())
    {
        return {"", ""};
    }

    // First, try to check a symbol in the executable
    auto in_executable = try_addr2line(executable, dr.address);
    if (valid_addr2line_return(in_executable))
    {
        auto function_name = try_addr2line(executable, dr.address, "-Cf");

        return {function_name, in_executable};
    }

    // Second, try to check a symbol in a library
    // We need the base address inside the library.
    Dl_info info;
    dladdr(hex_to_ptr(dr.address), &info);

    void *position_inside_lib = reinterpret_cast<void*>(
        (char*)hex_to_ptr(dr.address) - (char*)info.dli_fbase);

    std::ostringstream out;
    out << std::hex << position_inside_lib;
    std::string real_address = out.str();

    return {
        try_addr2line(executable, real_address, "-Cf"),
        try_addr2line(executable, real_address),
    };
}

void wf::print_trace(bool fast_mode)
{
    if (!fast_mode)
    {
    #if HAS_ASAN
        // We run with asan: just crash and let it print the stacktrace.
        int *p = 0;
        *p = 1;
    #endif
    }

    void *addrlist[MAX_FRAMES];
    int addrlen = backtrace(addrlist, MAX_FRAMES);
    if (addrlen == 0)
    {
        LOGE("Failed to determine backtrace, recompile with ASAN!");

        return;
    }

    char **symbollist = backtrace_symbols(addrlist, addrlen);

    for (int i = 1; i < addrlen; i++)
    {
        auto result = demangle_function(symbollist[i]);

        std::ostringstream line;
        line << '#' << std::left << std::setw(2) << i << " ";
        if (HAS_ADDR2LINE && !fast_mode && result.address.size() &&
            result.executable.size())
        {
            auto source = locate_source_file(result);
            line << source.function_name << " " << source.function_source;
        } else if (result.function_name.size())
        {
            line << result.function_name << " at " << result.address;
        } else
        {
            line << symbollist[i];
        }

        auto contents = line.str();
        if (contents.size() && (contents.back() == '\n'))
        {
            contents.pop_back();
        }

        wf::log::log_plain(wf::log::LOG_LEVEL_ERROR, contents);
    }

    free(symbollist);
}

#else // has <execinfo.h>
void wf::print_trace(bool)
{
    LOGE("Compiled without execinfo.h, cannot provide a backtrace!",
        " Try using address sanitizer.");
}

#endif

/* ------------------- Impl of debugging functions ---------------------------*/
#include <iomanip>
std::ostream& operator <<(std::ostream& out, const glm::mat4& mat)
{
    out << std::endl;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            out << std::setw(10) << std::fixed << std::setprecision(5) <<
                mat[j][i] << std::setw(0) << ",";
        }

        out << std::endl;
    }

    return out;
}

wf::pointf_t operator *(const glm::mat4& m, const wf::pointf_t& p)
{
    glm::vec4 v = {p.x, p.y, 0.0, 1.0};
    v = m * v;

    return wf::pointf_t{v.x, v.y};
}

wf::pointf_t operator *(const glm::mat4& m, const wf::point_t& p)
{
    return m * wf::pointf_t{1.0 * p.x, 1.0 * p.y};
}

std::ostream& wf::operator <<(std::ostream& out, wayfire_view view)
{
    if (view)
    {
        out << "view id=" << view->get_id() << " title=\"" << view->get_title() <<
            "\"" << " app_id=\"" << view->get_app_id() << "\"";
    } else
    {
        out << "(null view)";
    }

    return out;
}

std::bitset<(size_t)wf::log::logging_category::TOTAL> wf::log::enabled_categories;

#define CLEAR_COLOR "\033[0m"
#define GREY_COLOR "\033[30;1m"
#define GREEN_COLOR "\033[32;1m"
#define YELLOW_COLOR "\033[33;1m"
#define MAGENTA_COLOR "\033[35;1m"

template<class... Args>
static void color_debug_log(const char *color, Args... args)
{
    LOGD(color, args..., CLEAR_COLOR);
}

static std::string fmt_pointer(void *ptr)
{
    std::ostringstream ss;
    ss << ptr;
    return ss.str();
}

static void _dump_scene(wf::scene::node_ptr root, int depth = 0)
{
    using namespace wf::scene;

    // root
    // |-child
    // | |-nested
    // | | |-nested2
    //
    std::ostringstream node_line;
    for (int i = 0; i < depth; i++)
    {
        node_line << "|";
        if (i < depth - 1)
        {
            node_line << " ";
        } else
        {
            node_line << "-";
        }
    }

    node_line << root->stringify();
    node_line << " [" + fmt_pointer(root.get()) + "]";
    node_line << " geometry=" << root->get_bounding_box();

    const int greyed_flags = (int)node_flags::DISABLED;
    if (root->flags() & greyed_flags)
    {
        color_debug_log(GREY_COLOR, node_line.str());
    } else
    {
        color_debug_log(CLEAR_COLOR, node_line.str());
    }

    for (auto& ch : root->get_children())
    {
        _dump_scene(ch, depth + 1);
    }
}

void wf::dump_scene(scene::node_ptr root)
{
    _dump_scene(root);
}

void wf::detail::option_wrapper_debug_message(const std::string & option_name, const std::runtime_error & err)
{
    LOGE("Wayfire encountered error loading option \"", option_name, "\": ", err.what(), ". ",
        "Usual reasons for this include missing or outdated plugin XML files, ",
        "a bug in the plugin itself, or mismatch between the versions of Wayfire and wf-config. ",
        "Make sure that you have the correct versions of all relevant packages and make sure that there ",
        "are no conflicting installations of Wayfire using the same prefix.");
    wf::print_trace(false);
    std::_Exit(0);
}

void wf::detail::option_wrapper_debug_message(const std::string & option_name, const std::logic_error & err)
{
    LOGE("Wayfire encountered error loading option \"", option_name, "\": ", err.what(), ". ",
        "This usually indicates a bug in the plugin.");
    wf::print_trace(false);
    std::_Exit(0);
}
