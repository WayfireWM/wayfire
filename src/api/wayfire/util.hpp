#ifndef WF_UTIL_HPP
#define WF_UTIL_HPP

#include <algorithm>
#include <wayland-server.h>
#include <functional>

namespace wf
{
/** Convert timespect to milliseconds. */
int64_t timespec_to_msec(const timespec& ts);

/** Returns current time in msec, using CLOCK_MONOTONIC as a base */
uint32_t get_current_time();

/**
 * A wrapper around wl_listener compatible with C++11 std::functions
 */
struct wl_listener_wrapper
{
    using callback_t = std::function<void (void*)>;
    wl_listener_wrapper();
    ~wl_listener_wrapper();

    wl_listener_wrapper(const wl_listener_wrapper &) = delete;
    wl_listener_wrapper(wl_listener_wrapper &&) = delete;
    wl_listener_wrapper& operator =(const wl_listener_wrapper&) = delete;
    wl_listener_wrapper& operator =(wl_listener_wrapper&&) = delete;

    /** Set the callback to be used when the signal is fired. Can be called
     * multiple times to update it */
    void set_callback(callback_t call);
    /** Connect this callback to a signal. Calling this on an already
     * connected listener will have no effect.
     * @return true if connection was successful */
    bool connect(wl_signal *signal);
    /** Disconnect from the wl_signal. No-op if not connected */
    void disconnect();
    /** @return true if connected to a wl_signal */
    bool is_connected() const;
    /** Call the stored callback. No-op if no callback was specified */
    void emit(void *data);

    struct wrapper
    {
        wl_listener listener;
        wl_listener_wrapper *self;
    };

  private:
    callback_t call;
    wrapper _wrap;
};

/**
 * A wrapper for adding idle callbacks to the event loop
 */
class wl_idle_call
{
  public:
    using callback_t = std::function<void ()>;
    /* Initialize an empty idle call. */
    wl_idle_call();
    /** Will disconnect if connected */
    ~wl_idle_call();

    // Non-movable since wayland holds pointers to this object.
    wl_idle_call(const wl_idle_call &) = delete;
    wl_idle_call(wl_idle_call &&) = delete;
    wl_idle_call& operator =(const wl_idle_call&) = delete;
    wl_idle_call& operator =(wl_idle_call&&) = delete;

    /** Set the callback. This will disconnect the wl_idle_call if it is
     * connected */
    void set_callback(callback_t call);

    /** Run the passed callback the next time the loop goes idle. No effect
     * if already waiting for idleness, or if the callback hasn't been set. */
    void run_once();

    /* Same as calling set_callbck + run_once */
    void run_once(callback_t call);

    /** Stop waiting for idle, no-op if not connected */
    void disconnect();
    /** @return true if the event source is active */
    bool is_connected() const;

    /** execute the callback now. do not use manually! */
    void execute();

  private:
    callback_t call;
    wl_event_loop *loop     = NULL;
    wl_event_source *source = NULL;
};

/**
 * A wrapper for wl_event_loop_add_timer / wl_event_loop_timer_update
 */
class wl_timer
{
  public:
    // Return true if the timer should be fired again after the same amount of time
    using callback_t = std::function<bool ()>;

    wl_timer() = default;

    /** Disconnects the timer if connected */
    ~wl_timer();

    // Non-movable since wayland holds pointers to this object.
    wl_timer(const wl_timer &) = delete;
    wl_timer(wl_timer &&) = delete;
    wl_timer& operator =(const wl_timer&) = delete;
    wl_timer& operator =(wl_timer&&) = delete;

    /** Execute call after a timeout of timeout_ms */
    void set_timeout(uint32_t timeout_ms, callback_t call);

    /** If a timeout has been registered, but not fired yet, remove the
     * timeout. Otherwise no-op */
    void disconnect();
    /** @return true if the event source is active */
    bool is_connected();

    /* Run the stored call now, regardless of the timeout. No-op if not
     * connected */
    void execute();

  private:
    callback_t call;
    wl_event_source *source = NULL;
    uint32_t timeout = -1;
};
}

#endif /* end of include guard: WF_UTIL_HPP */
