#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/bindings-repository.hpp>
#include <wayfire/core.hpp>
#include <wayfire/nonstd/json.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/signal-definitions.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <unistd.h>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../../support/headless-core-harness.hpp"

namespace
{
class scoped_env_t
{
    std::string name;
    std::string old_value;
    bool had_old_value = false;

  public:
    scoped_env_t(std::string name, std::string value) : name(std::move(name))
    {
        if (const char *old = getenv(this->name.c_str()))
        {
            had_old_value = true;
            old_value     = old;
        }

        setenv(this->name.c_str(), value.c_str(), 1);
    }

    ~scoped_env_t()
    {
        if (had_old_value)
        {
            setenv(name.c_str(), old_value.c_str(), 1);
        } else
        {
            unsetenv(name.c_str());
        }
    }
};

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

class ipc_client_t
{
    int fd = -1;

  public:
    explicit ipc_client_t(const std::string& path)
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

    ~ipc_client_t()
    {
        close_client();
    }

    ipc_client_t(const ipc_client_t&) = delete;
    ipc_client_t(ipc_client_t&&) = delete;
    ipc_client_t& operator =(const ipc_client_t&) = delete;
    ipc_client_t& operator =(ipc_client_t&&) = delete;

    void close_client()
    {
        if (fd >= 0)
        {
            close(fd);
            fd = -1;
        }
    }

    void send(const wf::json_t& message)
    {
        std::string payload;
        message.map_serialized([&] (const char *buffer, size_t size)
        {
            payload.assign(buffer, size);
        });

        uint32_t size = payload.size();
        REQUIRE(write_exact(fd, reinterpret_cast<const char*>(&size), sizeof(size)));
        REQUIRE(write_exact(fd, payload.data(), payload.size()));
    }

    std::optional<wf::json_t> try_read()
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
};

static wf::json_t ipc_message(const std::string& method, wf::json_t data = {})
{
    wf::json_t message;
    message["method"] = method;
    message["data"]   = std::move(data);
    return message;
}

static wf::json_t read_message(wf::test::headless_core_harness_t& harness,
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

static wf::json_t call_method(wf::test::headless_core_harness_t& harness,
    ipc_client_t& client, const std::string& method, wf::json_t data = {})
{
    client.send(ipc_message(method, std::move(data)));
    return read_message(harness, client);
}

static void emit_key_release(uint32_t keycode)
{
    wlr_keyboard_key_event key_event = {};
    key_event.keycode = keycode;
    key_event.state   = WL_KEYBOARD_KEY_STATE_RELEASED;

    wf::input_event_signal<wlr_keyboard_key_event> signal;
    signal.event  = &key_event;
    signal.device = nullptr;
    wf::get_core().emit(&signal);
}
}

TEST_CASE("command repeat IPC binding disconnect does not crash Wayfire")
{
    const auto ipc_path = (std::filesystem::temp_directory_path() /
        ("wayfire-command-test-" + std::to_string(getpid()) + ".socket")).string();
    unlink(ipc_path.c_str());

    scoped_env_t plugin_path{"WAYFIRE_PLUGIN_PATH", TEST_PLUGIN_PATH};
    scoped_env_t ipc_socket{"_WAYFIRE_SOCKET", ipc_path};

    wf::test::headless_core_harness_t harness{
        "[core]\n"
        "plugins = ipc command\n"
        "\n"
        "[input]\n"
        "kb_repeat_delay = 30\n"
        "kb_repeat_rate = 20\n",
        true};

    REQUIRE(harness.run_until([&] { return std::filesystem::exists(ipc_path); }));

    ipc_client_t main_client{ipc_path};
    ipc_client_t binding_client{ipc_path};

    wf::json_t binding;
    binding["binding"] = "<super> KEY_F";
    binding["mode"]    = "repeat";
    binding["exec-always"] = true;
    auto register_response = call_method(harness, binding_client,
        "command/register-binding", binding);
    REQUIRE(register_response.has_member("binding-id"));

    wf::keybinding_t key{WLR_MODIFIER_LOGO, KEY_F};
    REQUIRE(wf::get_core().bindings->handle_key(key, 0));

    auto event = read_message(harness, binding_client);
    REQUIRE(event.has_member("event"));
    REQUIRE(event["event"].as_string() == "command-binding");

    binding_client.close_client();

    for (int i = 0; i < 20; ++i)
    {
        harness.dispatch_once(10);
    }

    auto ping_response = call_method(harness, main_client, "list-methods");
    CHECK(ping_response.has_member("methods"));

    emit_key_release(KEY_F);
}

TEST_CASE("command repeat IPC binding unregister and disconnect does not crash Wayfire")
{
    const auto ipc_path = (std::filesystem::temp_directory_path() /
        ("wayfire-command-unregister-test-" + std::to_string(getpid()) + ".socket")).string();
    unlink(ipc_path.c_str());

    scoped_env_t plugin_path{"WAYFIRE_PLUGIN_PATH", TEST_PLUGIN_PATH};
    scoped_env_t ipc_socket{"_WAYFIRE_SOCKET", ipc_path};

    wf::test::headless_core_harness_t harness{
        "[core]\n"
        "plugins = ipc command\n"
        "\n"
        "[input]\n"
        "kb_repeat_delay = 30\n"
        "kb_repeat_rate = 20\n",
        true};

    REQUIRE(harness.run_until([&] { return std::filesystem::exists(ipc_path); }));

    ipc_client_t main_client{ipc_path};
    ipc_client_t binding_client{ipc_path};

    wf::json_t binding;
    binding["binding"] = "<super> KEY_F";
    binding["mode"]    = "repeat";
    binding["exec-always"] = true;
    auto register_response = call_method(harness, binding_client,
        "command/register-binding", binding);
    REQUIRE(register_response.has_member("binding-id"));

    wf::keybinding_t key{WLR_MODIFIER_LOGO, KEY_F};
    REQUIRE(wf::get_core().bindings->handle_key(key, 0));

    auto event = read_message(harness, binding_client);
    REQUIRE(event.has_member("event"));
    REQUIRE(event["event"].as_string() == "command-binding");

    wf::json_t unregister_data;
    unregister_data["binding-id"] = register_response["binding-id"];
    auto unregister_response = call_method(harness, binding_client,
        "command/unregister-binding", unregister_data);
    REQUIRE(unregister_response.has_member("result"));

    binding_client.close_client();

    for (int i = 0; i < 20; ++i)
    {
        harness.dispatch_once(10);
    }

    auto ping_response = call_method(harness, main_client, "list-methods");
    CHECK(ping_response.has_member("methods"));

    emit_key_release(KEY_F);
}
