#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <map>

namespace wf
{
namespace ipc
{
/**
 * An IPC method has a name and a callback. The callback is a simple function which takes a json object which
 * contains the method's parameters and returns the result of the operation.
 */
using method_callback = std::function<nlohmann::json(nlohmann::json)>;

/**
 * The IPC method repository keeps track of all registered IPC methods. It can be used even without the IPC
 * plugin itself, as it facilitates inter-plugin calls similarly to signals.
 *
 * The method_repository_t is a singleton and is accessed by creating a shared_data::ref_ptr_t to it.
 */
class method_repository_t
{
  public:
    /**
     * Register a new method to the method repository. If the method already exists, the old handler will be
     * overwritten.
     */
    void register_method(std::string method, method_callback handler)
    {
        this->methods[method] = handler;
    }

    /**
     * Remove the last registered handler for the given method.
     */
    void unregister_method(std::string method)
    {
        this->methods.erase(method);
    }

    /**
     * Call an IPC method with the given name and given parameters.
     * If the method was not registered, a JSON object containing an error will be returned.
     */
    nlohmann::json call_method(std::string method, nlohmann::json data)
    {
        if (this->methods.count(method))
        {
            return this->methods[method](std::move(data));
        }

        return {
            {"error", "No such method found!"}
        };
    }

  private:
    std::map<std::string, method_callback> methods;
};

// A few helper definitions for IPC method implementations.
inline nlohmann::json json_ok()
{
    return nlohmann::json{
        {"result", "ok"}
    };
}

inline nlohmann::json json_error(std::string msg)
{
    return nlohmann::json{
        {"error", std::string(msg)}
    };
}

#define WFJSON_EXPECT_FIELD(data, field, type) \
    if (!data.count(field)) \
    { \
        return wf::ipc::json_error("Missing \"" field "\""); \
    } \
    else if (!data[field].is_ ## type()) \
    { \
        return wf::ipc::json_error("Field \"" field "\" does not have the correct type " #type); \
    }

#define WFJSON_OPTIONAL_FIELD(data, field, type) \
    if (data.count(field) && !data[field].is_ ## type()) \
    { \
        return wf::ipc::json_error("Field \"" field "\" does not have the correct type " #type); \
    }
}
}
