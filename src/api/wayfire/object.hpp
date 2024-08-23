#ifndef OBJECT_HPP
#define OBJECT_HPP

#include <typeinfo>
#include <memory>
#include <string>
#include <cstdint>

#include <wayfire/nonstd/observer_ptr.h>

namespace wf
{
/**
 * Subclasses of custom_data_t can be stored inside an object_base_t
 */
class custom_data_t
{
  public:
    custom_data_t() = default;
    virtual ~custom_data_t() = default;
    custom_data_t(custom_data_t&& other) = default;
    custom_data_t(const custom_data_t& other) = default;
    custom_data_t& operator =(custom_data_t&& other) = default;
    custom_data_t& operator =(const custom_data_t& other) = default;
};

/**
 * A base class for "objects". Objects provide signals and ways for plugins to
 * store custom data about the object.
 */
class object_base_t
{
  public:
    /** Get a human-readable description of the object */
    std::string to_string() const;

    /** Get the ID of the object. Each object has a unique ID */
    uint32_t get_id() const;

    /**
     * Retrieve custom data stored with the given name. If no such data exists,
     * then it is created with the default constructor.
     *
     * REQUIRES a default constructor
     * If your type doesn't have one, use store_data + get_data
     */
    template<class T>
    nonstd::observer_ptr<T> get_data_safe(
        std::string name = typeid(T).name())
    {
        auto data = get_data<T>(name);
        if (data)
        {
            return data;
        } else
        {
            store_data<T>(std::make_unique<T>(), name);

            return get_data<T>(name);
        }
    }

    /* Retrieve custom data stored with the given name. If no such
     * data exists, NULL is returned */
    template<class T>
    nonstd::observer_ptr<T> get_data(
        std::string name = typeid(T).name())
    {
        return nonstd::make_observer(dynamic_cast<T*>(_fetch_data(name)));
    }

    /* Assigns the given data to the given name */
    template<class T>
    void store_data(std::unique_ptr<T> stored_data,
        std::string name = typeid(T).name())
    {
        _store_data(std::move(stored_data), name);
    }

    /* Returns true if there is saved data under the given name */
    template<class T>
    bool has_data()
    {
        return has_data(typeid(T).name());
    }

    /** @return true if there is saved data with the given name */
    bool has_data(std::string name);

    /** Remove the saved data under the given name */
    void erase_data(std::string name);

    /** Remove the saved data for the type T */
    template<class T>
    void erase_data()
    {
        erase_data(typeid(T).name());
    }

    /* Erase the saved data from the store and return the pointer */
    template<class T>
    std::unique_ptr<T> release_data(
        std::string name = typeid(T).name())
    {
        if (!has_data(name))
        {
            return {nullptr};
        }

        auto stored = _fetch_erase(name);

        return std::unique_ptr<T>(dynamic_cast<T*>(stored));
    }

    virtual ~object_base_t();

    object_base_t(const object_base_t &) = delete;
    object_base_t(object_base_t &&) = delete;
    object_base_t& operator =(const object_base_t&) = delete;
    object_base_t& operator =(object_base_t&&) = delete;

  protected:
    object_base_t();

    /** Clear all stored data. */
    void _clear_data();

  private:
    /** Just get the data under the given name, or nullptr, if it does not exist */
    custom_data_t *_fetch_data(std::string name);
    /** Get the data under the given name, and release the pointer, deleting
     * the entry in the map */
    custom_data_t *_fetch_erase(std::string name);

    /** Store the given data under the given name */
    void _store_data(std::unique_ptr<custom_data_t> data, std::string name);

    class obase_impl;
    std::unique_ptr<obase_impl> obase_priv;
};
}

#endif /* end of include guard: OBJECT_HPP */
