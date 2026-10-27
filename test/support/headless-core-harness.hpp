#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <wayfire/core.hpp>

namespace wf::test
{
class headless_core_harness_t
{
  public:
    explicit headless_core_harness_t(std::string extra_config = {}, bool start_plugins = false);
    ~headless_core_harness_t();

    headless_core_harness_t(const headless_core_harness_t&) = delete;
    headless_core_harness_t(headless_core_harness_t&&) = delete;
    headless_core_harness_t& operator =(const headless_core_harness_t&) = delete;
    headless_core_harness_t& operator =(headless_core_harness_t&&) = delete;

    void dispatch_once(int timeout_ms = 0);
    void roundtrip();
    bool run_until(const std::function<bool()>& predicate, int max_iterations = 200);
    void enable_pointer_input();
    void enable_touch_input();
    void touch_down(int32_t id, double x, double y);
    void touch_motion(int32_t id, double x, double y);
    void touch_up(int32_t id);
    void touch_frame();
    void pointer_motion(double x, double y);
    void pointer_button(uint32_t button, uint32_t state);

    /**
     * Send a vertical axis event from the given pointer device and return the
     * delta after the configured scroll speed has been applied.
     * The device must have been announced via backend->events.new_input.
     * Note that the core skips the scaling if an axis binding consumes the
     * event, in which case the delta is returned unchanged.
     */
    double pointer_axis(wlr_pointer *device, wl_pointer_axis_source source, double delta);

    wf::output_t *output() const;
    const std::string& socket_name() const;
    std::vector<uint32_t> capture_output_pixels();

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}
