#include "ipc.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include <climits>
#include <wayfire/util/log.hpp>
#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>

/**
 * Handle WL_EVENT_READABLE on the socket.
 * Indicates a new connection.
 */
int wl_loop_handle_ipc_fd_connection(int, uint32_t, void *data)
{
    (*((std::function<void()>*)data))();
    return 0;
}

wf::ipc::server_t::server_t()
{
    accept_new_client = [=] ()
    {
        do_accept_new_client();
    };
}

void wf::ipc::server_t::init(std::string socket_path)
{
    this->fd = setup_socket(socket_path.c_str());
    if (fd == -1)
    {
        LOGE("Failed to create debug IPC socket!");
        return;
    }

    listen(fd, 3);
    source = wl_event_loop_add_fd(wl_display_get_event_loop(wf::get_core().display),
        fd, WL_EVENT_READABLE, wl_loop_handle_ipc_fd_connection, &accept_new_client);
}

wf::ipc::server_t::~server_t()
{
    if (fd != -1)
    {
        close(fd);
        unlink(saddr.sun_path);
        wl_event_source_remove(source);
    }
}

int wf::ipc::server_t::setup_socket(const char *address)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1)
    {
        return -1;
    }

    if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
    {
        return -1;
    }

    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
    {
        return -1;
    }

    // Ensure no old instance left after a crash or similar
    unlink(address);

    saddr.sun_family = AF_UNIX;
    strncpy(saddr.sun_path, address, sizeof(saddr.sun_path) - 1);

    int r = bind(fd, (sockaddr*)&saddr, sizeof(saddr));
    if (r != 0)
    {
        LOGE("Failed to bind debug IPC socket at address ", address, " !");
        // TODO: shutdown socket?
        return -1;
    }

    return fd;
}

void wf::ipc::server_t::do_accept_new_client()
{
    // Heavily inspired by Sway
    int cfd = accept(this->fd, NULL, NULL);
    if (cfd == -1)
    {
        LOGW("Error accepting client connection");
        return;
    }

    int flags;
    if (((flags = fcntl(cfd, F_GETFD)) == -1) ||
        (fcntl(cfd, F_SETFD, flags | FD_CLOEXEC) == -1))
    {
        LOGE("Failed setting CLOEXEC");
        close(cfd);
        return;
    }

    if (((flags = fcntl(cfd, F_GETFL)) == -1) ||
        (fcntl(cfd, F_SETFL, flags | O_NONBLOCK) == -1))
    {
        LOGE("Failed setting NONBLOCK");
        close(cfd);
        return;
    }

    this->clients.push_back(std::make_unique<client_t>(this, cfd));
}

void wf::ipc::server_t::client_disappeared(client_t *client)
{
    LOGD("Removing IPC client ", client);

    client_disconnected_signal ev;
    ev.client = client;
    method_repository->emit(&ev);

    auto it = std::remove_if(clients.begin(), clients.end(),
        [&] (const auto& cl) { return cl.get() == client; });
    clients.erase(it, clients.end());
}

void wf::ipc::server_t::handle_incoming_message(
    client_t *client, wf::json_t message)
{
    client->send_json(method_repository->call_method(message["method"], message["data"], client));
}

/* --------------------------- Per-client code ------------------------------*/

int wl_loop_handle_ipc_client_fd_event(int, uint32_t mask, void *data)
{
    (*((std::function<void(uint32_t)>*)data))(mask);
    return 0;
}

static constexpr int MAX_MESSAGE_LEN = (1 << 20);
static constexpr int HEADER_LEN = 4;

wf::ipc::client_t::client_t(server_t *ipc, int fd)
{
    LOGD("New IPC client, fd ", fd);

    this->fd  = fd;
    this->ipc = ipc;

    auto ev_loop = wf::get_core().ev_loop;
    source = wl_event_loop_add_fd(ev_loop, fd, WL_EVENT_READABLE,
        wl_loop_handle_ipc_client_fd_event, &this->handle_fd_activity);

    // +1 for null byte at the end
    buffer.resize(MAX_MESSAGE_LEN + 1);
    this->handle_fd_activity = [=] (uint32_t event_mask)
    {
        handle_fd_incoming(event_mask);
    };
}

// -1 error, 0 success, 1 try again later
int wf::ipc::client_t::read_up_to(int n, int *available)
{
    int need = n - current_buffer_valid;
    int want = std::min(need, *available);

    while (want > 0)
    {
        int r = read(fd, buffer.data() + current_buffer_valid, want);
        if (r <= 0)
        {
            LOGI("Read: EOF or error (%d) %s\n", r, strerror(errno));
            return -1;
        }

        want -= r;
        *available -= r;
        current_buffer_valid += r;
    }

    if (current_buffer_valid < n)
    {
        // didn't read all n bytes
        return 1;
    }

    return 0;
}

void wf::ipc::client_t::handle_fd_incoming(uint32_t event_mask)
{
    if (event_mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP))
    {
        ipc->client_disappeared(this);
        // this no longer exists
        return;
    }

    int available = 0;
    if (ioctl(this->fd, FIONREAD, &available) != 0)
    {
        LOGE("Failed to inspect message buffer!");
        ipc->client_disappeared(this);
        return;
    }

    while (available > 0)
    {
        if (current_buffer_valid < HEADER_LEN)
        {
            if (read_up_to(HEADER_LEN, &available) < 0)
            {
                ipc->client_disappeared(this);
                return;
            }

            continue;
        }

        const uint32_t len = *((uint32_t*)buffer.data());
        if (len > MAX_MESSAGE_LEN - HEADER_LEN)
        {
            LOGE("Client tried to pass too long a message!");
            ipc->client_disappeared(this);
            return;
        }

        const int next_target = HEADER_LEN + len;
        int r = read_up_to(next_target, &available);
        if (r < 0)
        {
            ipc->client_disappeared(this);
            return;
        }

        if (r > 0)
        {
            // Try again
            continue;
        }

        // Finally, received the message, make sure we have a terminating NULL byte
        buffer[current_buffer_valid] = '\0';
        char *str = buffer.data() + HEADER_LEN;

        json_t message;
        auto err = json_t::parse_string(std::string_view{str, len}, message);
        if (err.has_value())
        {
            json_t error;
            error["error"] = std::string("Client's message could not be parsed, error: ") + *err;
            LOGE((std::string)error["error"], ": ", str);
            this->send_json(error);
            ipc->client_disappeared(this);
            return;
        }

        if (!message.has_member("method") || !message["method"].is_string())
        {
            LOGI("Start");
            json_t error;
            error["error"] = "Client's message does not contain a method to be called!";
            LOGI("MID");
            LOGE(error["error"].as_string());
            LOGI("END");

            this->send_json(error);
            ipc->client_disappeared(this);
            return;
        }

        ipc->handle_incoming_message(this, std::move(message));
        // Reset for next message
        current_buffer_valid = 0;
    }
}

wf::ipc::client_t::~client_t()
{
    wl_event_source_remove(source);
    shutdown(fd, SHUT_RDWR);
    close(this->fd);
}

static bool write_exact(int fd, const char *buf, ssize_t n)
{
    while (n > 0)
    {
        ssize_t w = write(fd, buf, n);
        if (w <= 0)
        {
            return false;
        }

        n   -= w;
        buf += w;
    }

    return true;
}

bool wf::ipc::client_t::send_json(wf::json_t json)
{
    bool status = false;
    json.map_serialized([&] (const char *buffer, size_t size)
    {
        if (size > MAX_MESSAGE_LEN)
        {
            LOGE("Error sending json to client: message too long!");
            shutdown(fd, SHUT_RDWR);
            return;
        }

        uint32_t len = size;
        if (!write_exact(fd, (char*)&len, 4) || !write_exact(fd, buffer, len))
        {
            LOGE("Error sending json to client!");
            shutdown(fd, SHUT_RDWR);
            return;
        }

        status = true;
    });

    return status;
}

namespace wf
{
class ipc_plugin_t : public wf::plugin_interface_t
{
  private:
    shared_data::ref_ptr_t<ipc::server_t> server;

  public:
    void init() override
    {
        char *pre_socket  = getenv("_WAYFIRE_SOCKET");
        const auto& dname = wf::get_core().wayland_display;
        pid_t pid = getpid();
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

        std::string socket;
        if (pre_socket)
        {
            socket = pre_socket;
        } else if (runtime_dir)
        {
            socket = std::string(runtime_dir) + "/wayfire-" + dname + "-" + ".socket";
        } else
        {
            socket = "/tmp/wayfire-" + dname + "-" + std::to_string(pid) + ".socket";
        }

        setenv("WAYFIRE_SOCKET", socket.c_str(), 1);
        server->init(socket);
    }

    bool is_unloadable() override
    {
        return false;
    }

    int get_order_hint() const override
    {
        return INT_MIN;
    }
};
} // namespace wf

DECLARE_WAYFIRE_PLUGIN(wf::ipc_plugin_t);
