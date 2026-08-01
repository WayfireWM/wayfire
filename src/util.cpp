#include <wayfire/util.hpp>
#include <wayfire/region.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/core.hpp>
#include <ctime>
#include <cerrno>
#include <cstring>
#include <sys/timerfd.h>
#include <unistd.h>

#include "wl-listener-wrapper.tpp"

/* Misc helper functions */
int64_t wf::timespec_to_msec(const timespec& ts)
{
    return ts.tv_sec * 1000ll + ts.tv_nsec / 1000000ll;
}

int64_t wf::get_current_time()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return wf::timespec_to_msec(ts);
}

static void handle_idle_listener(void *data)
{
    auto call = (wf::wl_idle_call*)(data);
    call->execute();
}

static int handle_timeout(void *data)
{
    (*((std::function<void()>*)data))();
    return 0;
}

namespace wf
{
wl_idle_call::wl_idle_call() = default;
wl_idle_call::~wl_idle_call()
{
    disconnect();
}

void wl_idle_call::set_callback(callback_t call)
{
    disconnect();
    this->call = call;
}

wl_event_loop*wl_idle_call::loop = NULL;

void wl_idle_call::run_once()
{
    if (!call || source)
    {
        return;
    }

    auto use_loop = loop ?: get_core().ev_loop;
    source = wl_event_loop_add_idle(use_loop, handle_idle_listener, this);
}

void wl_idle_call::run_once(callback_t cb)
{
    set_callback(cb);
    run_once();
}

void wl_idle_call::disconnect()
{
    if (!source)
    {
        return;
    }

    wl_event_source_remove(source);
    source = nullptr;
}

bool wl_idle_call::is_connected() const
{
    return source;
}

void wl_idle_call::execute()
{
    source = nullptr;
    if (call)
    {
        call();
    }
}

wl_high_resolution_timer::~wl_high_resolution_timer()
{
    if (source)
    {
        wl_event_source_remove(source);
    }

    if (timer_fd >= 0)
    {
        close(timer_fd);
    }
}

bool wl_high_resolution_timer::ensure_source()
{
    if (source)
    {
        return true;
    }

    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (timer_fd < 0)
    {
        LOGE("Failed to create high-resolution timer: ", strerror(errno));
        return false;
    }

    source = wl_event_loop_add_fd(get_core().ev_loop, timer_fd,
        WL_EVENT_READABLE, handle_timer, this);
    if (!source)
    {
        LOGE("Failed to add high-resolution timer to the event loop");
        close(timer_fd);
        timer_fd = -1;
        return false;
    }

    return true;
}

bool wl_high_resolution_timer::set_deadline(int64_t deadline_ns, callback_t call)
{
    if ((deadline_ns <= 0) || !ensure_source())
    {
        return false;
    }

    itimerspec timer{};
    timer.it_value.tv_sec  = deadline_ns / 1'000'000'000;
    timer.it_value.tv_nsec = deadline_ns % 1'000'000'000;
    if (timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &timer, nullptr) < 0)
    {
        LOGE("Failed to arm high-resolution timer: ", strerror(errno));
        disconnect();
        return false;
    }

    this->call = std::move(call);
    armed = true;
    return true;
}

void wl_high_resolution_timer::disconnect()
{
    if ((timer_fd >= 0) && armed)
    {
        itimerspec timer{};
        timerfd_settime(timer_fd, 0, &timer, nullptr);
    }

    call  = {};
    armed = false;
}

bool wl_high_resolution_timer::is_connected() const
{
    return armed;
}

int wl_high_resolution_timer::handle_timer(int fd, uint32_t mask, void *data)
{
    auto timer = static_cast<wl_high_resolution_timer*>(data);
    uint64_t expirations;
    if (!(mask & WL_EVENT_READABLE) || (read(fd, &expirations, sizeof(expirations)) < 0))
    {
        return 0;
    }

    timer->armed = false;
    auto call = std::move(timer->call);
    timer->call = {};
    if (call)
    {
        call();
    }

    return 0;
}

template<bool Repeat>
wl_timer<Repeat>::~wl_timer()
{
    if (source)
    {
        wl_event_source_remove(source);
    }
}

template<bool Repeat>
void wl_timer<Repeat>::set_timeout(uint32_t timeout_ms, callback_t call)
{
    if (timeout_ms == 0)
    {
        disconnect();
        call();
        return;
    }

    this->execute = [=] ()
    {
        if constexpr (Repeat)
        {
            if (call())
            {
                wl_event_source_timer_update(source, this->timeout);
            } else
            {
                disconnect();
            }
        } else
        {
            // Disconnect first, ensuring that if `this` is destroyed, we don't use it anymore.
            disconnect();
            call();
        }
    };

    this->timeout = timeout_ms;
    if (!source)
    {
        source = wl_event_loop_add_timer(get_core().ev_loop, handle_timeout, &execute);
    }

    wl_event_source_timer_update(source, timeout_ms);
}

template<bool Repeat>
void wl_timer<Repeat>::disconnect()
{
    if (source)
    {
        wl_event_source_remove(source);
    }

    source = NULL;
}

template<bool Repeat>
bool wl_timer<Repeat>::is_connected()
{
    return source != NULL;
}

template class wl_timer<false>;

template class wl_timer<true>;
} // namespace wf
