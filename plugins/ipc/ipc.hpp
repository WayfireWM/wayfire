#pragma once

#include <nlohmann/json.hpp>
#include <sys/un.h>
#include <wayfire/object.hpp>
#include <variant>
#include <wayland-server.h>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include "ipc-method-repository.hpp"

namespace wf
{
namespace ipc
{
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
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> method_repository;

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
    server_t(std::string socket_path);
    ~server_t();

    // non-copyable, non-movable
    server_t(const server_t&) = delete;
    server_t(server_t&&) = delete;
    server_t& operator =(const server_t&) = delete;
    server_t& operator =(server_t&&) = delete;

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
    std::vector<std::unique_ptr<client_t>> clients;
};
}
}
