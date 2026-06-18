#include "ipc-client.hpp"

#include "headless-core-harness.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <utility>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace
{
static void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if ((flags < 0) || (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0))
    {
        throw std::system_error(errno, std::generic_category(), "fcntl");
    }
}

static bool write_exact(int fd, const char *buffer, size_t size)
{
    while (size > 0)
    {
        ssize_t written = write(fd, buffer, size);
        if (written < 0)
        {
            if ((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK))
            {
                continue;
            }

            return false;
        }

        buffer += written;
        size   -= written;
    }

    return true;
}
}

wf::test::ipc_client_t::ipc_client_t(const std::string& path)
{
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        throw std::system_error(errno, std::generic_category(), "socket");
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
    {
        throw std::runtime_error("IPC socket path is too long");
    }

    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        throw std::system_error(errno, std::generic_category(), "connect");
    }

    set_nonblock(fd);
}

wf::test::ipc_client_t::~ipc_client_t()
{
    close_client();
}

void wf::test::ipc_client_t::close_client()
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

void wf::test::ipc_client_t::send(const wf::json_t& message)
{
    std::string payload;
    message.map_serialized([&] (const char *buffer, size_t size)
    {
        payload.assign(buffer, size);
    });

    uint32_t size = payload.size();
    if (!write_exact(fd, reinterpret_cast<const char*>(&size), sizeof(size)) ||
        !write_exact(fd, payload.data(), payload.size()))
    {
        throw std::runtime_error("Failed to write IPC message");
    }
}

std::optional<wf::json_t> wf::test::ipc_client_t::try_read()
{
    uint32_t size;
    ssize_t r = read(fd, &size, sizeof(size));
    if (r < 0)
    {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR))
        {
            return std::nullopt;
        }

        throw std::system_error(errno, std::generic_category(), "read header");
    }

    if (r == 0)
    {
        throw std::runtime_error("IPC socket closed while reading header");
    }

    if (r != sizeof(size))
    {
        throw std::runtime_error("Short IPC header read");
    }

    std::string payload(size, '\0');
    size_t offset = 0;
    while (offset < payload.size())
    {
        r = read(fd, payload.data() + offset, payload.size() - offset);
        if (r < 0)
        {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR))
            {
                continue;
            }

            throw std::system_error(errno, std::generic_category(), "read payload");
        }

        if (r == 0)
        {
            throw std::runtime_error("IPC socket closed while reading payload");
        }

        offset += r;
    }

    wf::json_t result;
    auto error = wf::json_t::parse_string(payload, result);
    if (error.has_value())
    {
        throw std::runtime_error("Failed to parse IPC response: " + *error);
    }

    return result;
}

wf::json_t wf::test::ipc_message(const std::string& method, wf::json_t data)
{
    wf::json_t message;
    message["method"] = method;
    message["data"]   = std::move(data);
    return message;
}

wf::json_t wf::test::read_message(headless_core_harness_t& harness,
    ipc_client_t& client)
{
    for (int i = 0; i < 200; ++i)
    {
        harness.dispatch_once(1);
        if (auto msg = client.try_read())
        {
            return *msg;
        }
    }

    throw std::runtime_error("Timed out waiting for IPC message");
}

wf::json_t wf::test::call_method(headless_core_harness_t& harness,
    ipc_client_t& client, const std::string& method, wf::json_t data)
{
    client.send(ipc_message(method, std::move(data)));
    return read_message(harness, client);
}
