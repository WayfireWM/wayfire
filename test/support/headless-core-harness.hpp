#pragma once

#include <functional>
#include <memory>
#include <string>
#include <wayfire/core.hpp>

namespace wf::test
{
class headless_core_harness_t
{
  public:
    headless_core_harness_t();
    ~headless_core_harness_t();

    headless_core_harness_t(const headless_core_harness_t&) = delete;
    headless_core_harness_t(headless_core_harness_t&&) = delete;
    headless_core_harness_t& operator =(const headless_core_harness_t&) = delete;
    headless_core_harness_t& operator =(headless_core_harness_t&&) = delete;

    void dispatch_once(int timeout_ms = 0);
    void roundtrip();
    bool run_until(const std::function<bool()>& predicate, int max_iterations = 200);

    wf::output_t *output() const;
    const std::string& socket_name() const;

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}
