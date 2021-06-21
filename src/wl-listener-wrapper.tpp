/** Implementation for wf::wl_listener_wrapper */
#include <wayfire/util.hpp>

namespace wf
{
static void handle_wrapped_listener(wl_listener *listener, void *data)
{
    wf::wl_listener_wrapper::wrapper *wrap =
        wl_container_of(listener, wrap, listener);
    wrap->self->emit(data);
}

wl_listener_wrapper::wl_listener_wrapper()
{
    _wrap.self = this;
    _wrap.listener.notify = handle_wrapped_listener;
    wl_list_init(&_wrap.listener.link);
}

wl_listener_wrapper::~wl_listener_wrapper()
{
    disconnect();
}

void wl_listener_wrapper::set_callback(callback_t _call)
{
    this->call = _call;
}

bool wl_listener_wrapper::connect(wl_signal *signal)
{
    if (is_connected())
    {
        return false;
    }

    wl_signal_add(signal, &_wrap.listener);

    return true;
}

void wl_listener_wrapper::disconnect()
{
    wl_list_remove(&_wrap.listener.link);
    wl_list_init(&_wrap.listener.link);
}

bool wl_listener_wrapper::is_connected() const
{
    return !wl_list_empty(&_wrap.listener.link);
}

void wl_listener_wrapper::emit(void *data)
{
    if (this->call)
    {
        this->call(data);
    }
}
}
