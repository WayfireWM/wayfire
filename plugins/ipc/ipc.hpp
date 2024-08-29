#pragma once

#include <nlohmann/json.hpp>
#include <sys/un.h>
#include <wayfire/object.hpp>
#include <wayland-server.h>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include "ipc-method-repository.hpp"
#include <netinet/in.h>

namespace wf
{
namespace ipc
{
/**
 * Represents a single connected client to the IPC socket.
 */
class server_t;
class client_t : public client_interface_t
{
  public:
    client_t(server_t *server, int client_fd);
    ~client_t();
    void send_json(nlohmann::json json) override;

  private:
    int fd;
    wl_event_source *source;
    server_t *ipc;

    int current_buffer_valid = 0;
    std::vector<char> buffer;
    int read_up_to(int n, int *available);

    /** Handle incoming data on the socket */
    std::function<void(uint32_t)> handle_fd_activity;
    void handle_fd_incoming(uint32_t);
};

/**
 * The IPC server is a singleton object accessed via shared_data::ref_ptr_t.
 * It represents the IPC socket used for communication with clients.
 */
class server_t
{
  public:
    server_t();
    void init(int port);
    ~server_t();

  private:
    friend class client_t;
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> method_repository;

    void handle_incoming_message(client_t *client, nlohmann::json message);

    void client_disappeared(client_t *client);

    int fd = -1;

    /**
     * Setup a socket at the given address, and set it as CLOEXEC and non-blocking.
     */
    int setup_socket(int port);
    sockaddr_in saddr;
    wl_event_source *source;
    std::vector<std::unique_ptr<client_t>> clients;

    std::function<void()> accept_new_client;
    void do_accept_new_client();
};
}
}
