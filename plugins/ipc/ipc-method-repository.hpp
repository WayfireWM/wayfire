#pragma once

#include <nlohmann/json.hpp> // IWYU pragma: keep
#include <functional>
#include <map>
#include "wayfire/signal-provider.hpp"

namespace wf
{
namespace ipc
{
/**
 * A client_interface_t represents a client which has connected to the IPC socket.
 * It can be used by plugins to send back data to a specific client.
 */
class client_interface_t
{
  public:
    virtual bool send_json(nlohmann::json json) = 0; // Returns true upon success.
    virtual ~client_interface_t() = default;
};

/**
 * A signal emitted on the ipc method repository when a client disconnects.
 */
struct client_disconnected_signal
{
    client_interface_t *client;
};

/**
 * An IPC method has a name and a callback. The callback is a simple function which takes a json object which
 * contains the method's parameters and returns the result of the operation.
 */
using method_callback = std::function<nlohmann::json(nlohmann::json)>;

/**
 * Same as @method_callback, but also supports getting information about the connected ipc client.
 */
using method_callback_full = std::function<nlohmann::json(nlohmann::json, client_interface_t*)>;

/**
 * The IPC method repository keeps track of all registered IPC methods. It can be used even without the IPC
 * plugin itself, as it facilitates inter-plugin calls similarly to signals.
 *
 * The method_repository_t is a singleton and is accessed by creating a shared_data::ref_ptr_t to it.
 */
class method_repository_t : public wf::signal::provider_t
{
  public:
    /**
     * Register a new method to the method repository. If the method already exists, the old handler will be
     * overwritten.
     */
    void register_method(std::string method, method_callback_full handler)
    {
        this->methods[method] = handler;
    }

    /**
     * Register a new method to the method repository. If the method already exists, the old handler will be
     * overwritten.
     */
    void register_method(std::string method, method_callback handler)
    {
        this->methods[method] = [handler] (const nlohmann::json& data, client_interface_t*)
        {
            return handler(data);
        };
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
    nlohmann::json call_method(std::string method, nlohmann::json data,
        client_interface_t *client = nullptr)
    {
        if (this->methods.count(method))
        {
            return this->methods[method](std::move(data), client);
        }

        return {
            {"error", "No such method found!"}
        };
    }

    method_repository_t()
    {
        register_method("list-methods", [this] (auto)
        {
            nlohmann::json response;
            response["methods"] = nlohmann::json::array();
            for (auto& [method, _] : methods)
            {
                response["methods"].push_back(method);
            }

            return response;
        });
    }

  private:
    std::map<std::string, method_callback_full> methods;
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
        return wf::ipc::json_error("Field \"" + std::string(field) + \
    "\" does not have the correct type " #type); \
    }
}
}
