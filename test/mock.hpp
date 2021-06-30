#pragma once

#include <wayfire/util.hpp>
#include <map>
#include <list>
#include <memory>

/**
 * A mock event loop with idle and timer sources.
 *
 * Neither are dispatched automatically.
 * Instead, tests can (re)start the loop and manually dispatch either idle
 * or timers from the next X milliseconds.
 */
class mock_loop
{
  public:
    static mock_loop& get();

    // Reset time to X and remove any sources
    void start(int X);

    // Dispatch idle sources
    void dispatch_idle();

    // Forward time by X ms, dispatching any timers
    void move_forward(int ms);

    // Add a periodic timer for after X ms
    void add_timer(wf::wl_timer::callback_t *callback, int ms);

    // Unregister a timer
    void rem_timer(wf::wl_timer::callback_t *callback);

    // Add an idle callback
    void add_idle(wf::wl_idle_call::callback_t *callback);

    // Unregiser an idle callback
    void rem_idle(wf::wl_idle_call::callback_t *callback);

    // Get current time
    int ctime();

  private:
    struct timer_item
    {
        wf::wl_timer::callback_t *cb;
        int period;
    };

    std::multimap<int, timer_item> timers;
    std::list<std::unique_ptr<wf::wl_idle_call::callback_t*>> idles;
    int current_time = 0;
};
