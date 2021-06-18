#ifndef WF_SAFE_LIST_HPP
#define WF_SAFE_LIST_HPP

#include <list>
#include <memory>
#include <algorithm>
#include <functional>
#include <wayfire/util.hpp>

#include "reverse.hpp"

/* This is a trimmed-down wrapper of std::list<T>.
 *
 * It supports safe iteration over all elements in the collection, where any
 * element can be deleted from the list at any given time (i.e even in a
 * for-each-like loop) */
namespace wf
{
template<class T>
class safe_list_t
{
    wf::wl_idle_call idle_cleanup;
    std::list<std::unique_ptr<T>> list;

    /* Remove all invalidated elements in the list */
    std::function<void()> do_cleanup = [&] ()
    {
        auto it = list.begin();
        while (it != list.end())
        {
            if (*it)
            {
                ++it;
            } else
            {
                it = list.erase(it);
            }
        }
    };

    /* Return whether the list has invalidated elements */
    bool is_dirty() const
    {
        return idle_cleanup.is_connected();
    }

  public:
    safe_list_t()
    {}

    /* Copy the not-erased elements from other, but do not copy the idle source */
    safe_list_t(const safe_list_t& other)
    {
        *this = other;
    }

    safe_list_t& operator =(const safe_list_t& other)
    {
        this->idle_cleanup_source = NULL;
        other.for_each([&] (auto& el)
        {
            this->push_back(el);
        });
    }

    safe_list_t(safe_list_t&& other) = default;
    safe_list_t& operator =(safe_list_t&& other) = default;

    T& back()
    {
        /* No invalidated elements */
        if (!is_dirty())
        {
            return *list.back();
        }

        auto it = list.rbegin();
        while (it != list.rend() && (*it) == nullptr)
        {
            ++it;
        }

        if (it == list.rend())
        {
            throw std::out_of_range("back() called on an empty list!");
        }

        return **it;
    }

    size_t size() const
    {
        if (!is_dirty())
        {
            return list.size();
        }

        /* Count non-null elements, because that's the real size */
        size_t sz = 0;
        for (auto& it : list)
        {
            sz += (it != nullptr);
        }

        return sz;
    }

    /* Push back by copying */
    void push_back(T value)
    {
        list.push_back(std::make_unique<T>(std::move(value)));
    }

    /* Push back by moving */
    void emplace_back(T&& value)
    {
        list.push_back(std::make_unique<T>(value));
    }

    enum insert_place_t
    {
        INSERT_BEFORE,
        INSERT_AFTER,
        INSERT_NONE,
    };

    /* Insert the given value at a position in the list, determined by the
     * check function. The value is inserted at the first position that
     * check indicates, or at the end of the list otherwise */
    void emplace_at(T&& value, std::function<insert_place_t(T&)> check)
    {
        auto it = list.begin();
        while (it != list.end())
        {
            /* Skip empty elements */
            if (*it == nullptr)
            {
                ++it;
                continue;
            }

            auto place = check(**it);
            switch (place)
            {
              case INSERT_AFTER:
                /* We can safely increment it, because it points to an
                 * element in the list */
                ++it;

              // fall through
              case INSERT_BEFORE:
                list.emplace(it, std::make_unique<T>(value));

                return;

              default:
                break;
            }

            ++it;
        }

        /* If no place found, insert at the end */
        emplace_back(std::move(value));
    }

    void insert_at(T value, std::function<insert_place_t(T&)> check)
    {
        emplace_at(std::move(value), check);
    }

    /* Call func for each non-erased element of the list */
    void for_each(std::function<void(T&)> func) const
    {
        /* Go through all elements currently in the list */
        auto it = list.begin();
        for (int size = list.size(); size > 0; size--, it++)
        {
            if (*it)
            {
                func(**it);
            }
        }
    }

    /* Call func for each non-erased element of the list in reversed order */
    void for_each_reverse(std::function<void(T&)> func) const
    {
        auto it = list.rbegin();
        for (int size = list.size(); size > 0; size--, it++)
        {
            if (*it)
            {
                func(**it);
            }
        }
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
        bool actually_removed = false;
        for (auto& it : list)
        {
            if (it && predicate(*it))
            {
                actually_removed = true;
                /* First reset the element in the list, and then free resources */
                auto copy = std::move(it);
                it = nullptr;
                /* Now copy goes out of scope */
            }
        }

        /* Schedule a clean-up, but be careful to not schedule it twice */
        if (!is_dirty() && actually_removed)
        {
            idle_cleanup.run_once([this] () { this->do_cleanup(); });
        }
    }
};
}

#endif /* end of include guard: WF_SAFE_LIST_HPP */
