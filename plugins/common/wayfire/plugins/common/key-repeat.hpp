#pragma once

#include <wayfire/option-wrapper.hpp>
#include <wayfire/util.hpp>

namespace wf
{
struct key_repeat_t
{
    wf::option_wrapper_t<int> delay{"input/kb_repeat_delay"};
    wf::option_wrapper_t<int> rate{"input/kb_repeat_rate"};

    wf::wl_timer<false> timer_delay;
    wf::wl_timer<true> timer_rate;

    using callback_t = std::function<bool (uint32_t)>;

    key_repeat_t()
    {}
    key_repeat_t(uint32_t key, callback_t handler)
    {
        set_callback(key, handler);
    }

    void set_callback(uint32_t key, callback_t handler)
    {
        disconnect();
        timer_delay.set_timeout(delay, [=] ()
        {
            timer_rate.set_timeout(1000 / rate, [=] ()
            {
                // handle can determine if key should be repeated
                return handler(key);
            });
        });
    }

    void disconnect()
    {
        timer_delay.disconnect();
        timer_rate.disconnect();
    }
};
}
