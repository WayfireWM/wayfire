#ifndef WF_SAFE_LIST_HPP
#define WF_SAFE_LIST_HPP

#include <algorithm>
#include <functional>
#include <optional>
#include <type_traits>
#include <vector>
#include <assert.h>

namespace wf
{
/**
 * The safe list is a trimmed down version of std::list<T>.
 *
 * It supports adding an item to the end of a list, iterating over it and erasing elements.
 * The important advantage is that elements can be callbacks, which may be executed during the iteration,
 * and the callbacks can then add or remove elements from the list safely.
 *
 * The typical usage of safe list is for bindings and signal handlers.
 */
template<class T>
class safe_list_t
{
    static_assert(std::is_move_constructible_v<std::optional<T>>, "T must be moveable!");

  public:
    safe_list_t()
    {}

    T& back()
    {
        auto it = list.rbegin();
        assert((it != list.rend()) && "back() on an empty list!");

        while (!((*it).has_value()))
        {
            ++it;
            assert((it != list.rend()) && "back() on an empty list!");
        }

        return **it;
    }

    size_t size() const
    {
        if (!is_dirty)
        {
            return list.size();
        }

        return std::count_if(list.begin(), list.end(), [] (const auto& elem) { return elem.has_value(); });
    }

    /* Push back by copying */
    void push_back(T value)
    {
        list.push_back({std::move(value)});
    }

    /* Call func for each non-erased element of the list */
    void for_each(std::function<void(T&)> func)
    {
        _start_iter();

        // Important: make sure we do not iterate over additional values in the list which are added
        // afterwards.
        size_t size = list.size();
        for (size_t i = 0; i < size; i++)
        {
            if (list[i])
            {
                func(*list[i]);
            }
        }

        _stop_iter();
    }

    /* Call func for each non-erased element of the list in reversed order */
    void for_each_reverse(std::function<void(T&)> func)
    {
        _start_iter();
        for (size_t i = list.size(); i > 0; i--)
        {
            if (list[i - 1])
            {
                func(*list[i - 1]);
            }
        }

        _stop_iter();
    }

    /* Safely remove all elements equal to value */
    void remove_all(const T& value)
    {
        remove_if([=] (const T& el) { return el == value; });
    }

    /* Remove all elements from the list */
    void clear()
    {
        remove_if([] (const T&) { return true; });
    }

    /* Remove all elements satisfying a given condition.
     * This function resets their pointers and scheduling a cleanup operation */
    void remove_if(std::function<bool(const T&)> predicate)
    {
        _start_iter();

        const size_t size = list.size();
        for (size_t i = 0; i < size; i++)
        {
            if (list[i] && predicate(*list[i]))
            {
                /* First reset the element in the list, and then free resources */
                auto value = std::move(list[i]);
                list[i].reset();
                is_dirty = true;

                // Call destructor
                value.reset();
            }
        }

        _stop_iter();
        _try_cleanup();
    }

  private:
    /**
     * A vector containing the values of the list.
     * To make sure we can iterate over the list and erase any elements from it during iteration, the 'erase'
     * operation simply resets the optional value in the list.
     *
     * After all iterations are done, the list is 'cleaned up', that is, empty elements are removed from it.
     */
    std::vector<std::optional<T>> list;

    int iteration_counter = 0;
    bool is_dirty = false;

    /* Remove all invalidated elements in the list */
    void _try_cleanup()
    {
        if ((iteration_counter > 0) || !is_dirty)
        {
            // There is an active iteration.
            return;
        }

        auto it = std::remove_if(list.begin(), list.end(),
            [&] (const std::optional<T>& elem) { return !elem.has_value(); });
        list.erase(it, list.end());
        is_dirty = false;
    }

    void _start_iter()
    {
        ++iteration_counter;
    }

    void _stop_iter()
    {
        --iteration_counter;
        _try_cleanup();
    }
};
}

#endif /* end of include guard: WF_SAFE_LIST_HPP */
