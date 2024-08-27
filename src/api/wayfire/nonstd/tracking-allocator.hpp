#pragma once
#include <memory>
#include <functional>
#include <algorithm>
#include <wayfire/dassert.hpp>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/signal-provider.hpp>

namespace wf
{
/**
 * The destruct signal is emitted directly before an object is freed.
 * Emitted on objects managed with the tracking allocator which support signals.
 */
template<class T>
struct destruct_signal
{
    T *object;
};

/**
 * The tracking allocator is a factory singleton for allocating objects of a certain type.
 * The objects are allocated via shared pointers, and the tracking allocator keeps a list of all allocated
 * objects, accessible by plugins.
 */
template<class ObjectType>
class tracking_allocator_t
{
  public:
    /**
     * Get the single global instance of the tracking allocator.
     */
    static tracking_allocator_t<ObjectType>& get()
    {
        static tracking_allocator_t<ObjectType> allocator;
        return allocator;
    }

    template<class ConcreteObjectType, class... Args>
    std::shared_ptr<ConcreteObjectType> allocate(Args... args)
    {
        static_assert(std::is_base_of_v<ObjectType, ConcreteObjectType>);
        auto ptr = std::shared_ptr<ConcreteObjectType>(
            new ConcreteObjectType(std::forward<Args>(args)...),
            std::bind(&tracking_allocator_t<ObjectType>::deallocate_object, this, std::placeholders::_1));

        allocated_objects.push_back(ptr.get());
        return ptr;
    }

    const std::vector<nonstd::observer_ptr<ObjectType>>& get_all()
    {
        return allocated_objects;
    }

  private:
    std::vector<nonstd::observer_ptr<ObjectType>> allocated_objects;
    void deallocate_object(ObjectType *obj)
    {
        if constexpr (std::is_base_of_v<wf::signal::provider_t, ObjectType>)
        {
            destruct_signal<ObjectType> event;
            event.object = obj;
            obj->emit(&event);
        }

        auto it = std::find(allocated_objects.begin(), allocated_objects.end(),
            nonstd::observer_ptr<ObjectType>{obj});
        wf::dassert(it != allocated_objects.end(), "Object is not allocated?");
        allocated_objects.erase(it);
        delete obj;
    }
};
}
