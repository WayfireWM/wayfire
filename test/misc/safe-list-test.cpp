#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/nonstd/safe-list.hpp>

TEST_CASE("Safe-list basics")
{
    wf::safe_list_t<int> list;

    list.push_back(5);
    list.push_back(6);
    REQUIRE(list.size() == 2);

    list.for_each([x = 0] (int i) mutable
    {
        if (x == 0)
        {
            REQUIRE(i == 5);
        }

        if (x == 1)
        {
            REQUIRE(i == 6);
        }

        REQUIRE(x <= 1);
        x++;
    });

    list.remove_if([] (int i) { return i == 5; });
    REQUIRE(list.size() == 1);

    list.for_each([x = 0] (int i) mutable
    {
        if (x == 0)
        {
            REQUIRE(i == 6);
        }

        REQUIRE(x <= 0);
        x++;
    });
}

TEST_CASE("safe-list remove self")
{
    using cb = std::function<void ()>;
    wf::safe_list_t<cb*> list;

    cb self;
    list.push_back(&self);
    list.for_each_reverse([&] (cb *c)
    {
        REQUIRE(c == &self);
        list.remove_all(c);
    });
}

TEST_CASE("safe-list remove next")
{
    using cb = std::function<void ()>;
    wf::safe_list_t<cb*> list;

    cb self, next;
    list.push_back(&self);
    list.push_back(&next);

    int calls = 0;
    list.for_each([&] (cb *c)
    {
        calls++;
        REQUIRE(calls <= 1);
        REQUIRE(c == &self);
        list.remove_all(&next);
        REQUIRE(list.back() == &self);
    });
}

TEST_CASE("safe-list push next")
{
    using cb = std::function<void ()>;
    wf::safe_list_t<cb*> list;

    cb self, next;
    list.push_back(&self);

    int calls = 0;
    list.for_each([&] (cb *c)
    {
        calls++;
        REQUIRE(calls <= 1);
        list.push_back(&next);
    });

    REQUIRE(list.size() == 2);
}
