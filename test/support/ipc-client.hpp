#pragma once

#include <optional>
#include <string>
#include <wayfire/nonstd/json.hpp>

namespace wf::test
{
class headless_core_harness_t;

class ipc_client_t
{
    int fd = -1;

  public:
    explicit ipc_client_t(const std::string& path);
    ~ipc_client_t();

    ipc_client_t(const ipc_client_t&) = delete;
    ipc_client_t(ipc_client_t&&) = delete;
    ipc_client_t& operator =(const ipc_client_t&) = delete;
    ipc_client_t& operator =(ipc_client_t&&) = delete;

    void close_client();
    void send(const wf::json_t& message);
    std::optional<wf::json_t> try_read();
};

wf::json_t ipc_message(const std::string& method, wf::json_t data = {});
wf::json_t read_message(headless_core_harness_t& harness, ipc_client_t& client);
wf::json_t call_method(headless_core_harness_t& harness,
    ipc_client_t& client, const std::string& method, wf::json_t data = {});
}
