#pragma once

#include <wayfire/object.hpp>
#include <wayfire/core.hpp>

namespace wf
{
/**
 * The purpose of shared is to allow multiple plugins or plugin instances to
 * have shared global custom data.
 *
 * While this is already possible if the shared data is stored as custom data on
 * `wf::get_core()`, the classes here provide convenient wrappers for managing
 * the lifetime of the shared data by utilizing RAII.
 */
namespace shared_data
{
namespace detail
{
/** Implementation detail: the actual data stored in core. */
template<class T>
class shared_data_t : public wf::custom_data_t
{
  public:
    T data;
    int32_t use_count = 0;
};
}

/**
 * A pointer to shared data which holds a reference to it (similar to
 * std::shared_ptr). Once the last reference is destroyed, data will be freed
 * from core.
 */
template<class T>
class ref_ptr_t
{
  public:
    ref_ptr_t()
    {
        update_use_count(+1);
        this->data = &wf::get_core().get_data_safe<detail::shared_data_t<T>>()->data;
    }

    ref_ptr_t(const ref_ptr_t<T>& other)
    {
        this->data = other.data;
        update_use_count(+1);
    }

    ref_ptr_t& operator =(const ref_ptr_t<T>& other)
    {
        this->data = other.data;
        update_use_count(+1);
    }

    ref_ptr_t(ref_ptr_t<T>&& other) = default;
    ref_ptr_t& operator =(ref_ptr_t<T>&& other) = default;

    ~ref_ptr_t()
    {
        update_use_count(-1);
    }

    T *get()
    {
        return data;
    }

    T*operator ->()
    {
        return data;
    }

  private:
    // Update the use count, and delete data if necessary.
    void update_use_count(int32_t delta)
    {
        auto instance = wf::get_core().get_data_safe<detail::shared_data_t<T>>();
        instance->use_count += delta;
        if (instance->use_count <= 0)
        {
            wf::get_core().erase_data<detail::shared_data_t<T>>();
        }
    }

    // Pointer to the global data
    T *data;
};
}
}
