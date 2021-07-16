#ifndef WF_UTIL_HPP
#define WF_UTIL_HPP

#include <algorithm>
#include <functional>
#include <pixman.h>

#include "wayfire/geometry.hpp"

/* ---------------------- pixman utility functions -------------------------- */
namespace wf
{
struct region_t
{
    region_t();
    /* Makes a copy of the given region */
    region_t(pixman_region32_t *damage);
    region_t(const wlr_box& box);
    ~region_t();

    region_t(const region_t& other);
    region_t(region_t&& other);

    region_t& operator =(const region_t& other);
    region_t& operator =(region_t&& other);

    bool empty() const;
    void clear();

    void expand_edges(int amount);
    pixman_box32_t get_extents() const;
    bool contains_point(const point_t& point) const;
    bool contains_pointf(const pointf_t& point) const;

    /* Translate the region */
    region_t operator +(const point_t& vector) const;
    region_t& operator +=(const point_t& vector);

    region_t operator *(float scale) const;
    region_t& operator *=(float scale);

    /* Region intersection */
    region_t operator &(const wlr_box& box) const;
    region_t operator &(const region_t& other) const;
    region_t& operator &=(const wlr_box& box);
    region_t& operator &=(const region_t& other);

    /* Region union */
    region_t operator |(const wlr_box& other) const;
    region_t operator |(const region_t& other) const;
    region_t& operator |=(const wlr_box& other);
    region_t& operator |=(const region_t& other);

    /* Subtract the box/region from the current region */
    region_t operator ^(const wlr_box& box) const;
    region_t operator ^(const region_t& other) const;
    region_t& operator ^=(const wlr_box& box);
    region_t& operator ^=(const region_t& other);

    pixman_region32_t *to_pixman();

    const pixman_box32_t *begin() const;
    const pixman_box32_t *end() const;

  private:
    pixman_region32_t _region;
    /* Returns a const-casted pixman_region32_t*, useful in const operators
     * where we use this->_region as only source for calculations, but pixman
     * won't let us pass a const pixman_region32_t* */
    pixman_region32_t *unconst() const;
};
}

wlr_box wlr_box_from_pixman_box(const pixman_box32_t& box);
pixman_box32_t pixman_box_from_wlr_box(const wlr_box& box);

/* ------------------------- misc helper functions ------------------------- */
namespace wf
{
/** Convert timespect to milliseconds. */
int64_t timespec_to_msec(const timespec& ts);

/** Returns current time in msec, using CLOCK_MONOTONIC as a base */
uint32_t get_current_time();

/** Return the closest valume to @value which is in [@min, @max] */
template<class T>
T clamp(T value, T min, T max)
{
    return std::min(std::max(value, min), max);
}

/**
 * Return the closest geometry to window which is completely inside the output.
 * The returned geometry might be smaller, but never bigger than window.
 */
geometry_t clamp(geometry_t window, geometry_t output);
}

namespace wf
{
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
    /* Initialize an empty idle call. set_event_loop() and set_callback()
     * should be called before calls to run_once(), otherwise it won't
     * have any effect */
    wl_idle_call();
    /** Will disconnect if connected */
    ~wl_idle_call();

    // Non-movable since wayland holds pointers to this object.
    wl_idle_call(const wl_idle_call &) = delete;
    wl_idle_call(wl_idle_call &&) = delete;
    wl_idle_call& operator =(const wl_idle_call&) = delete;
    wl_idle_call& operator =(wl_idle_call&&) = delete;

    /** Set the event loop. This will disconnect the wl_idle_call if it
     * is connected. If no event loop is set (or if NULL loop), the default
     * loop from core is used */
    void set_event_loop(wl_event_loop *loop);

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
    bool is_connected();

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
