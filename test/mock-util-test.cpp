#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mock.hpp"

TEST_CASE("Event loop idles")
{
    mock_loop ml;
    ml.start(0);

    int cnt_dispatched1 = 0;
    int cnt_dispatched2 = 0;
    wf::wl_idle_call::callback_t cb = [&] ()
    {
        ++cnt_dispatched1;
    };

    wf::wl_idle_call::callback_t cb2 = [&] ()
    {
        ++cnt_dispatched2;
    };

    ml.add_idle(&cb);
    ml.add_idle(&cb2);
    ml.add_idle(&cb2);

    REQUIRE(cnt_dispatched1 == 0);
    REQUIRE(cnt_dispatched2 == 0);

    int require1 = 1;
    int require2 = 2;

    SUBCASE("Dispatch all")
    {
        // dispatch both in this subcase
    }

    SUBCASE("Dispatch only 1")
    {
        // remove callback 2 and make sure it is not called
        ml.rem_idle(&cb2);
        require2 = 0;
    }

    ml.dispatch_idle();
    REQUIRE(cnt_dispatched1 == require1);
    REQUIRE(cnt_dispatched2 == require2);

    ml.dispatch_idle();
    REQUIRE(cnt_dispatched1 == require1);
    REQUIRE(cnt_dispatched2 == require2);
}

TEST_CASE("Event loop timers")
{
    int cnt_dispatched1 = 0;
    int cnt_dispatched2 = 0;
    wf::wl_timer::callback_t cb = [&] ()
    {
        ++cnt_dispatched1;
        return (cnt_dispatched1 < 4);
    };

    wf::wl_timer::callback_t cb2 = [&] ()
    {
        ++cnt_dispatched2;
        return false;
    };

    mock_loop ml;
    ml.add_timer(&cb, 100);
    ml.add_timer(&cb2, 50);
    REQUIRE(cnt_dispatched1 == 0);
    REQUIRE(cnt_dispatched2 == 0);


    SUBCASE("Remove 2")
    {
        ml.rem_timer(&cb2);
        ml.move_forward(100);

        REQUIRE(cnt_dispatched1 == 1);
        REQUIRE(cnt_dispatched2 == 0);

        ml.move_forward(100);

        REQUIRE(cnt_dispatched1 == 2);
        REQUIRE(cnt_dispatched2 == 0);
    }

    SUBCASE("Dispatch all")
    {
        ml.move_forward(299);
        REQUIRE(cnt_dispatched1 == 2);
        REQUIRE(cnt_dispatched2 == 1);

        ml.move_forward(100);
        REQUIRE(cnt_dispatched1 == 3);
        REQUIRE(cnt_dispatched2 == 1);

        ml.move_forward(1);
        REQUIRE(cnt_dispatched1 == 4);
        REQUIRE(cnt_dispatched2 == 1);

        ml.move_forward(1000);
        REQUIRE(cnt_dispatched1 == 4);
        REQUIRE(cnt_dispatched2 == 1);
    }
}

TEST_CASE("mock wl_idle_call")
{
    mock_loop::get().start(0);

    int cnt_dispatched1 = 0;
    int cnt_dispatched2 = 0;

    wf::wl_idle_call cb1;
    wf::wl_idle_call cb2;
    cb1.set_callback([&] ()
    {
        ++cnt_dispatched1;
    });

    cb2.set_callback([&] ()
    {
        ++cnt_dispatched2;
    });

    REQUIRE(!cb1.is_connected());
    REQUIRE(!cb2.is_connected());

    cb1.run_once();
    cb2.run_once();
    REQUIRE(cb1.is_connected());
    REQUIRE(cb2.is_connected());

    cb2.disconnect();

    REQUIRE(cb1.is_connected());
    REQUIRE(!cb2.is_connected());

    // Second dispatch is empty
    for (int i = 0; i < 2; i++)
    {
        mock_loop::get().dispatch_idle();
        REQUIRE(cnt_dispatched1 == 1);
        REQUIRE(cnt_dispatched2 == 0);

        REQUIRE(!cb1.is_connected());
        REQUIRE(!cb2.is_connected());
    }
}

TEST_CASE("Mock wl_timer")
{
    mock_loop::get().start(0);

    int cnt_dispatched1 = 0;
    int cnt_dispatched2 = 0;
    wf::wl_timer cb1, cb2;

    cb1.set_timeout(100, [&] ()
    {
        ++cnt_dispatched1;
        return (cnt_dispatched1 < 4);
    });

    REQUIRE(cb1.is_connected());
    REQUIRE(!cb2.is_connected());
    cb2.set_timeout(50, [&] ()
    {
        ++cnt_dispatched2;
        return (cnt_dispatched2 < 2);
    });
    REQUIRE(cb1.is_connected());
    REQUIRE(cb2.is_connected());
    REQUIRE(cnt_dispatched1 == 0);
    REQUIRE(cnt_dispatched2 == 0);

    mock_loop::get().move_forward(100);

    REQUIRE(cb1.is_connected());
    REQUIRE(!cb2.is_connected());
    REQUIRE(cnt_dispatched1 == 1);
    REQUIRE(cnt_dispatched2 == 2);

    mock_loop::get().move_forward(100);
    REQUIRE(cb1.is_connected());
    REQUIRE(!cb2.is_connected());
    REQUIRE(cnt_dispatched1 == 2);
    REQUIRE(cnt_dispatched2 == 2);

    cb1.disconnect();
    for (int i = 0; i < 3; i++)
    {
        REQUIRE(!cb1.is_connected());
        REQUIRE(!cb2.is_connected());
        REQUIRE(cnt_dispatched1 == 2);
        REQUIRE(cnt_dispatched2 == 2);
        mock_loop::get().move_forward(100);
    }
}
