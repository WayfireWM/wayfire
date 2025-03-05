#pragma once

#include <wayfire/nonstd/json.hpp>
#include "wayfire/geometry.hpp"
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/core.hpp>
#include "ipc-method-repository.hpp"

namespace wf
{
namespace ipc
{
#define WFJSON_GETTER_FUNCTION(type, ctype) \
    inline ctype json_get_ ## type(const wf::json_t& data, std::string field) \
    { \
        if (!data.has_member(field)) \
        { \
            throw ipc_method_exception_t("Missing \"" + field + "\""); \
        } \
        else if (!data[field].is_ ## type()) \
        { \
            throw ipc_method_exception_t( \
    "Field \"" + field + "\" does not have the correct type, expected " #type); \
        } \
\
        return (ctype)data[field]; \
    } \
 \
    inline std::optional<ctype> json_get_optional_ ## type(const wf::json_t& data, \
    std::string field) \
    { \
        if (!data.has_member(field)) \
        { \
            return {}; \
        } \
        else if (!data[field].is_ ## type()) \
        { \
            throw ipc_method_exception_t( \
    "Field \"" + field + "\" does not have the correct type, expected " #type); \
        } \
\
        return (ctype)data[field]; \
    }

WFJSON_GETTER_FUNCTION(int64, int64_t);
WFJSON_GETTER_FUNCTION(uint64, uint64_t);
WFJSON_GETTER_FUNCTION(double, double);
WFJSON_GETTER_FUNCTION(string, std::string);
WFJSON_GETTER_FUNCTION(bool, bool);

#undef WFJSON_GETTER_FUNCTION


inline wayfire_view find_view_by_id(uint32_t id)
{
    for (auto view : wf::get_core().get_all_views())
    {
        if (view->get_id() == id)
        {
            return view;
        }
    }

    return nullptr;
}

inline wf::output_t *find_output_by_id(int32_t id)
{
    for (auto wo : wf::get_core().output_layout->get_outputs())
    {
        if ((int)wo->get_id() == id)
        {
            return wo;
        }
    }

    return nullptr;
}

inline wf::workspace_set_t *find_workspace_set_by_index(int32_t index)
{
    for (auto wset : wf::workspace_set_t::get_all())
    {
        if ((int)wset->get_index() == index)
        {
            return wset.get();
        }
    }

    return nullptr;
}

inline wf::json_t geometry_to_json(wf::geometry_t g)
{
    wf::json_t j;
    j["x"]     = g.x;
    j["y"]     = g.y;
    j["width"] = g.width;
    j["height"] = g.height;
    return j;
}

#define CHECK(field, type) (j.has_member(field) && j[field].is_ ## type())

inline std::optional<wf::geometry_t> geometry_from_json(const wf::json_t& j)
{
    if (!CHECK("x", int) || !CHECK("y", int) ||
        !CHECK("width", int) || !CHECK("height", int))
    {
        return {};
    }

    return wf::geometry_t{
        .x     = j["x"],
        .y     = j["y"],
        .width = j["width"],
        .height = j["height"],
    };
}

inline wf::json_t point_to_json(wf::point_t p)
{
    wf::json_t j;
    j["x"] = p.x;
    j["y"] = p.y;
    return j;
}

inline std::optional<wf::point_t> point_from_json(const wf::json_t& j)
{
    if (!CHECK("x", int) || !CHECK("y", int))
    {
        return {};
    }

    return wf::point_t{
        .x = j["x"],
        .y = j["y"],
    };
}

inline wf::json_t dimensions_to_json(wf::dimensions_t d)
{
    wf::json_t j;
    j["width"]  = d.width;
    j["height"] = d.height;
    return j;
}

inline std::optional<wf::dimensions_t> dimensions_from_json(const wf::json_t& j)
{
    if (!CHECK("width", int) || !CHECK("height", int))
    {
        return {};
    }

    return wf::dimensions_t{
        .width  = j["width"],
        .height = j["height"],
    };
}

#undef CHECK
}
}
