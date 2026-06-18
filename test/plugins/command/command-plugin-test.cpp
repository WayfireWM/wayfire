#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/bindings-repository.hpp>
#include <wayfire/core.hpp>
#include <wayfire/nonstd/json.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/signal-definitions.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "../../support/headless-core-harness.hpp"
#include "../../support/ipc-client.hpp"

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

    wf::test::ipc_client_t main_client{ipc_path};
    wf::test::ipc_client_t binding_client{ipc_path};

    wf::json_t binding;
    binding["binding"] = "<super> KEY_F";
    binding["mode"]    = "repeat";
    binding["exec-always"] = true;
    auto register_response = wf::test::call_method(harness, binding_client,
        "command/register-binding", binding);
    REQUIRE(register_response.has_member("binding-id"));

    wf::keybinding_t key{WLR_MODIFIER_LOGO, KEY_F};
    REQUIRE(wf::get_core().bindings->handle_key(key, 0));

    auto event = wf::test::read_message(harness, binding_client);
    REQUIRE(event.has_member("event"));
    REQUIRE(event["event"].as_string() == "command-binding");

    binding_client.close_client();

    for (int i = 0; i < 20; ++i)
    {
        harness.dispatch_once(10);
    }

    auto ping_response = wf::test::call_method(harness, main_client, "list-methods");
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

    wf::test::ipc_client_t main_client{ipc_path};
    wf::test::ipc_client_t binding_client{ipc_path};

    wf::json_t binding;
    binding["binding"] = "<super> KEY_F";
    binding["mode"]    = "repeat";
    binding["exec-always"] = true;
    auto register_response = wf::test::call_method(harness, binding_client,
        "command/register-binding", binding);
    REQUIRE(register_response.has_member("binding-id"));

    wf::keybinding_t key{WLR_MODIFIER_LOGO, KEY_F};
    REQUIRE(wf::get_core().bindings->handle_key(key, 0));

    auto event = wf::test::read_message(harness, binding_client);
    REQUIRE(event.has_member("event"));
    REQUIRE(event["event"].as_string() == "command-binding");

    wf::json_t unregister_data;
    unregister_data["binding-id"] = register_response["binding-id"];
    auto unregister_response = wf::test::call_method(harness, binding_client,
        "command/unregister-binding", unregister_data);
    REQUIRE(unregister_response.has_member("result"));

    binding_client.close_client();

    for (int i = 0; i < 20; ++i)
    {
        harness.dispatch_once(10);
    }

    auto ping_response = wf::test::call_method(harness, main_client, "list-methods");
    CHECK(ping_response.has_member("methods"));

    emit_key_release(KEY_F);
}
