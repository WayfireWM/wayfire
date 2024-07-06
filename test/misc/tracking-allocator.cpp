#include "wayfire/nonstd/tracking-allocator.hpp"
#include "wayfire/signal-provider.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

class base_t : public wf::signal::provider_t
{
  public:
    using wf::signal::provider_t::provider_t;
    static int destroyed;
    virtual ~base_t()
    {
        ++destroyed;
    }
};

class derived_t : public base_t
{
  public:
    derived_t(int useless)
    {
        (void)useless;
    }

    virtual ~derived_t() = default;
};

int base_t::destroyed = 0;


TEST_CASE("Misc factory works")
{
    auto& allocator = wf::tracking_allocator_t<base_t>::get();
    REQUIRE(&allocator == &wf::tracking_allocator_t<base_t>::get());
    REQUIRE((void*)&allocator != (void*)&wf::tracking_allocator_t<derived_t>::get());

    auto obj_a = allocator.allocate<base_t>();
    REQUIRE(allocator.get_all().size() == 1);

    int destruct_events = 0;
    wf::signal::connection_t<wf::destruct_signal<base_t>> on_destroy;

    {
        auto obj_b = allocator.allocate<derived_t>(5);
        on_destroy = [&destruct_events, expected = obj_b.get()] (wf::destruct_signal<base_t> *ev)
        {
            REQUIRE(ev->object == expected);
            ++destruct_events;
        };

        obj_b->connect(&on_destroy);
        REQUIRE(allocator.get_all().size() == 2);
    }

    REQUIRE(destruct_events == 1);
    REQUIRE(allocator.get_all().size() == 1);
}
