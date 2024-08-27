#pragma once

#include "wayfire/geometry.hpp"
#include <json/value.h>
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/core.hpp>

namespace wf
{
namespace ipc
{
#define WFJSON_GETTER_FUNCTION(type, suffix, rtype) \
    inline rtype json_get_ ## suffix(const Json::Value& data, std::string field) \
    { \
        if (!data.isMember(field)) \
        { \
            throw ("Missing \"" + field + "\""); \
        } \
        else if (!data[field].is ## type()) \
        { \
            throw ("Field \"" + field + "\" does not have the correct type, expected " #type); \
        } \
\
        return data[field].as ## type(); \
    } \
 \
    inline std::optional<rtype> json_get_optional_ ## suffix(const Json::Value& data, std::string field) \
    { \
        if (!data.isMember(field)) \
        { \
            return {}; \
        } \
        else if (!data[field].is ## type()) \
        { \
            throw ("Field \"" + field + "\" does not have the correct type, expected " #type); \
        } \
\
        return data[field].as ## type(); \
    }

WFJSON_GETTER_FUNCTION(Int64, int64, int64_t);
WFJSON_GETTER_FUNCTION(UInt64, uint64, uint64_t);
WFJSON_GETTER_FUNCTION(Double, f64, double);
WFJSON_GETTER_FUNCTION(String, string, std::string);
WFJSON_GETTER_FUNCTION(Bool, bool, bool);

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

inline Json::Value geometry_to_json(wf::geometry_t g)
{
    Json::Value j;
    j["x"]     = g.x;
    j["y"]     = g.y;
    j["width"] = g.width;
    j["height"] = g.height;
    return j;
}

inline std::optional<wf::geometry_t> geometry_from_json(const Json::Value& j)
{
#define CHECK(field, type) (j.isMember(field) && j[field].is ## type())
    if (!CHECK("x", Int) || !CHECK("y", Int) ||
        !CHECK("width", Int) || !CHECK("height", Int))
    {
        return {};
    }

#undef CHECK

    return wf::geometry_t{
        .x     = j["x"].asInt(),
        .y     = j["y"].asInt(),
        .width = j["width"].asInt(),
        .height = j["height"].asInt(),
    };
}

inline Json::Value point_to_json(wf::point_t p)
{
    Json::Value j;
    j["x"] = p.x;
    j["y"] = p.y;
    return j;
}

inline std::optional<wf::point_t> point_from_json(const Json::Value& j)
{
#define CHECK(field, type) (j.isMember(field) && j[field].is ## type())
    if (!CHECK("x", Int) || !CHECK("y", Int))
    {
        return {};
    }

#undef CHECK

    return wf::point_t{
        .x = j["x"].asInt(),
        .y = j["y"].asInt(),
    };
}

inline Json::Value dimensions_to_json(wf::dimensions_t d)
{
    Json::Value j;
    j["width"]  = d.width;
    j["height"] = d.height;
    return j;
}

inline std::optional<wf::dimensions_t> dimensions_from_json(const Json::Value& j)
{
#define CHECK(field, type) (j.isMember(field) && j[field].is ## type())
    if (!CHECK("width", Int) || !CHECK("height", Int))
    {
        return {};
    }

#undef CHECK

    return wf::dimensions_t{
        .width  = j["width"].asInt(),
        .height = j["height"].asInt(),
    };
}
}
}
