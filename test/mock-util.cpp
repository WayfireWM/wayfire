#include <wayfire/util.hpp>
#include "mock.hpp"
#include "../src/wl-listener-wrapper.tpp"

namespace wf
{
/** Convert timespect to milliseconds. */
int64_t timespec_to_msec(const timespec& ts)
{
    return ts.tv_sec * 1000ll + ts.tv_nsec / 1000000ll;
}

uint32_t get_current_time()
{
    return mock_loop::get().ctime();
}
}

// Mock wl_idle_call
wf::wl_idle_call::wl_idle_call()
{}

wf::wl_idle_call::~wl_idle_call()
{
    disconnect();
}

void wf::wl_idle_call::set_callback(callback_t call)
{
    this->call = [this, call]
    {
        call();
        this->source = nullptr;
    };
}

void wf::wl_idle_call::run_once()
{
    if (this->call)
    {
        mock_loop::get().add_idle(&call);
        // Use the source as boolean flag for connectivity
        source = (wl_event_source*)0x1;
    }
}

void wf::wl_idle_call::run_once(callback_t call)
{
    set_callback(call);
    run_once();
}

void wf::wl_idle_call::disconnect()
{
    if (this->source)
    {
        this->source = nullptr;
        mock_loop::get().rem_idle(&call);
    }
}

bool wf::wl_idle_call::is_connected() const
{
    return source != nullptr;
}

void wf::wl_idle_call::execute()
{
    /* no-op here, because the callback is directly called by mock_loop */
}

// Mock wl_timer
wf::wl_timer::~wl_timer()
{
    disconnect();
}

void wf::wl_timer::set_timeout(uint32_t timeout_ms, callback_t call)
{
    this->source = (wl_event_source*)0x1;
    this->call   = [call, this] ()
    {
        if (call())
        {
            return true;
        }

        // mark as disconnected
        this->source = nullptr;
        return false;
    };

    mock_loop::get().add_timer(&this->call, timeout_ms);
}

void wf::wl_timer::disconnect()
{
    mock_loop::get().rem_timer(&call);
    source = nullptr;
}

bool wf::wl_timer::is_connected()
{
    return source != nullptr;
}

void wf::wl_timer::execute()
{
    /* no-op here, because the callback is directly called by mock_loop */
}

// Mock event loop
mock_loop& mock_loop::get()
{
    static mock_loop ml;
    return ml;
}

void mock_loop::start(int X)
{
    this->timers.clear();
    this->idles.clear();
    this->current_time = X;
}

int mock_loop::ctime()
{
    return current_time;
}

void mock_loop::dispatch_idle()
{
    for (auto it = idles.begin(); it != idles.end();)
    {
        auto cb = *it;
        ++it;
        (*cb)();
    }

    idles.clear();
}

void mock_loop::move_forward(int ms)
{
    current_time += ms;

    for (auto it = timers.begin(); it != timers.end();)
    {
        if (it->first <= current_time)
        {
            if ((*it->second.cb)())
            {
                timers.insert({it->first + it->second.period, it->second});
            }

            it = timers.erase(it);
        } else
        {
            break;
        }
    }
}

void mock_loop::add_timer(wf::wl_timer::callback_t *callback, int ms)
{
    timers.insert({ctime() + ms, timer_item{callback, ms}});
}

void mock_loop::rem_timer(wf::wl_timer::callback_t *callback)
{
    for (auto it = timers.begin(); it != timers.end();)
    {
        if (it->second.cb == callback)
        {
            it = timers.erase(it);
        } else
        {
            ++it;
        }
    }
}

void mock_loop::add_idle(wf::wl_idle_call::callback_t *callback)
{
    idles.push_back(callback);
}

void mock_loop::rem_idle(wf::wl_idle_call::callback_t *callback)
{
    for (auto it = idles.begin(); it != idles.end();)
    {
        if (*it == callback)
        {
            it = idles.erase(it);
        } else
        {
            ++it;
        }
    }
}
