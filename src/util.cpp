#include <wayfire/util.hpp>
#include <wayfire/region.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/core.hpp>
#include <ctime>

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
