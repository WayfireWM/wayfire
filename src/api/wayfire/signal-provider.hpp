#pragma once

#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <wayfire/nonstd/safe-list.hpp>
#include <cassert>
#include <typeindex>

namespace wf
{
namespace signal
{
class provider_t;

/**
 * A base class for all connection_t, needed to store list of connections in a
 * type-safe way.
 */
class connection_base_t
{
  public:
    connection_base_t(const connection_base_t&) = delete;
    connection_base_t(connection_base_t&&) = delete;
    connection_base_t& operator =(const connection_base_t&) = delete;
    connection_base_t& operator =(connection_base_t&&) = delete;

    /**
     * Automatically disconnects from every connected provider.
     */
    virtual ~connection_base_t()
    {
        disconnect();
    }

    bool is_connected() const
    {
        return !connected_to.empty();
    }

    /** Disconnect from all connected signal providers */
    void disconnect();

  protected:
    connection_base_t()
    {}

    // Allow provider to deregister itself
    friend class provider_t;
    std::unordered_set<provider_t*> connected_to;
};

/**
 * A connection to a signal on an object.
 * Uses RAII to automatically disconnect the signal when it goes out of scope.
 */
template<class SignalType>
class connection_t final : public connection_base_t
{
  public:
    using callback = std::function<void (SignalType*)>;

    /** Initialize an empty signal connection */
    connection_t()
    {}

    /** Automatically disconnects from all providers */
    virtual ~connection_t()
    {}

    template<class CallbackType> using convertible_to_callback_t =
        std::enable_if_t<std::is_constructible_v<callback, CallbackType>, void>;

    /** Initialize a signal connection with the given callback */
    template<class T, class U = convertible_to_callback_t<T>>
    connection_t(const T& callback) : connection_t()
    {
        set_callback(callback);
    }

    /** Initialize a signal connection with the given callback */
    template<class T, class U = convertible_to_callback_t<T>>
    connection_t& operator =(const T& callback)
    {
        set_callback(callback);
        return *this;
    }

    template<class T>
    connection_t(std::function<void(T*)>& callback) : connection_t()
    {
        set_callback(callback);
    }

    /** Set the signal callback or override the existing signal callback. */
    void set_callback(callback cb)
    {
        this->current_callback = cb;
    }

    /** Call the stored callback with the given data. */
    void emit(SignalType *data)
    {
        if (current_callback)
        {
            current_callback(data);
        }
    }

  private:
    // Non-copyable and non-movable, as that would require updating/duplicating
    // the signal handler. But this is usually not what users of this API want.
    // Also provider_t holds pointers to this object.
    connection_t(const connection_t&) = delete;
    connection_t(connection_t&&) = delete;
    connection_t& operator =(const connection_t&) = delete;
    connection_t& operator =(connection_t&&) = delete;

    callback current_callback;
};

class provider_t
{
  public:
    /**
     * Signals are designed to be useful for C++ plugins, however, they are
     * generally quite difficult to bind in other languages.
     * To avoid this problem, signal::provider_t also provides C-friendlier
     * callback support.
     *
     * The order of arguments is: (this_pointer, signal_name, data_pointer)
     */
    using c_api_callback = std::function<void (void*, const char*, void*)>;

    /** Register a connection to be called when the given signal is emitted. */
    template<class SignalType>
    void connect(connection_t<SignalType> *callback)
    {
        typed_connections[index<SignalType>()].push_back(callback);
        callback->connected_to.insert(this);
    }

    /** Unregister a connection. */
    void disconnect(connection_base_t *callback)
    {
        callback->connected_to.erase(this);
        for (auto& [id, connected] : typed_connections)
        {
            connected.remove_all(callback);
        }
    }

    /** Emit the given signal. */
    template<class SignalType>
    void emit(SignalType *data)
    {
        auto& conns = typed_connections[std::type_index(typeid(SignalType))];
        conns.for_each([&] (connection_base_t *tc)
        {
            auto real_type = dynamic_cast<connection_t<SignalType>*>(tc);
            assert(real_type);
            real_type->emit(data);
        });
    }

    provider_t()
    {}

    ~provider_t()
    {
        for (auto& [id, connected] : typed_connections)
        {
            connected.for_each([&] (connection_base_t *base)
            {
                base->connected_to.erase(this);
            });
        }
    }

    // Non-movable, non-copyable: connection_t keeps reference to this object.
    // Unclear what happens if this object is duplicated, and plugins usually
    // don't want this either.
    provider_t(const provider_t& other) = delete;
    provider_t& operator =(const provider_t& other) = delete;
    provider_t(provider_t&& other) = delete;
    provider_t& operator =(provider_t&& other) = delete;

  private:
    template<class SignalType>
    static inline std::type_index index()
    {
        return std::type_index(typeid(SignalType));
    }

    std::unordered_map<std::type_index, wf::safe_list_t<connection_base_t*>>
    typed_connections;
};
}
}
