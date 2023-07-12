#include "wayfire/nonstd/tracking-allocator.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

class base_t
{
  public:
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

    {
        auto obj_b = allocator.allocate<derived_t>(5);
        REQUIRE(allocator.get_all().size() == 2);
    }

    REQUIRE(allocator.get_all().size() == 1);
}
