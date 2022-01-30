#pragma once

#include <nlohmann/json.hpp>
#include <sys/un.h>
#include <wayfire/object.hpp>
#include <variant>
#include <wayland-server.h>

namespace wf
{
namespace ipc
{
struct error_response_t
{
    std::string message;
    nlohmann::json data;
};

/**
 * Represents a single connected client to the IPC.
 */
class server_t;
class client_t
{
  public:
    client_t(server_t *ipc, int fd);
    ~client_t();

    /** Handle incoming data on the socket */
    void handle_fd_activity(uint32_t event_mask);
    void send_json(nlohmann::json json);

  private:
    int fd;
    wl_event_source *source;
    server_t *ipc;

    int current_buffer_valid = 0;
    std::vector<char> buffer;
    int read_up_to(int n, int *available);

    client_t(const client_t&) = delete;
    client_t(client_t&&) = delete;
    client_t& operator =(const client_t&) = delete;
    client_t& operator =(client_t&&) = delete;
};

class server_t
{
  public:
    using method_cb = std::function<nlohmann::json(nlohmann::json)>;

    server_t(std::string socket_path);
    ~server_t();

    // Register a handler for the given method
    // It will get the params object from the request, and must return either a valid
    // json object indicating the
    // response or an error.
    void register_method(std::string method, method_cb handler);
    void unregister_method(std::string method);

    // non-copyable, non-movable
    server_t(const server_t&) = delete;
    server_t(server_t&&) = delete;
    server_t& operator =(const server_t&) = delete;
    server_t& operator =(server_t&&) = delete;

    nlohmann::json call_method(std::string method, nlohmann::json data);

    void accept_new_client();
    void client_disappeared(client_t *client);

  private:
    int fd;
    /**
     * Setup a socket at the given address, and set it as CLOEXEC and non-blocking.
     */
    int setup_socket(const char *address);

    sockaddr_un saddr;
    wl_event_source *source;

    std::map<std::string, method_cb> methods;
    std::vector<std::unique_ptr<client_t>> clients;
};
}
}
