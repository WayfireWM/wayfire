#pragma once

#include <json/value.h>
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
    virtual void send_json(Json::Value json) = 0;
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
using method_callback = std::function<Json::Value(Json::Value)>;

/**
 * Same as @method_callback, but also supports getting information about the connected ipc client.
 */
using method_callback_full = std::function<Json::Value(Json::Value, client_interface_t*)>;

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
        this->methods[method] = [handler] (const Json::Value& data, client_interface_t*)
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
    Json::Value call_method(std::string method, Json::Value data,
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
            Json::Value response;
            response["methods"] = Json::arrayValue;
            for (auto& [method, _] : methods)
            {
                response["methods"].append(method);
            }

            return response;
        });
    }

  private:
    std::map<std::string, method_callback_full> methods;
};

// A few helper definitions for IPC method implementations.
inline Json::Value json_ok()
{
    Json::Value r;
    r["result"] = "ok";
    return r;
}

inline Json::Value json_error(std::string msg)
{
    Json::Value r;
    r["error"] = msg;
    return r;
}
}
}
