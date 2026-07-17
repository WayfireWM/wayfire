#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/core.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>

#include <vector>

#include "../support/headless-core-harness.hpp"
#include "../support/wayland-xdg-client.hpp"

namespace
{
static void flush_touch(wf::test::headless_core_harness_t& harness,
    wf::test::wayland_xdg_client_t& client)
{
    harness.touch_frame();
    harness.dispatch_once();
    client.dispatch_once();
}
}

TEST_CASE("configured gesture finger count controls touch cancel threshold")
{
    wf::test::headless_core_harness_t harness{
        "[input]\n"
        "gesture_finger_count = 4\n"};
    harness.enable_touch_input();

    std::vector<wayfire_view> mapped;
    wf::signal::connection_t<wf::view_mapped_signal> on_map = [&] (wf::view_mapped_signal *ev)
    {
        mapped.push_back(ev->view);
    };
    wf::get_core().connect(&on_map);

    wf::test::wayland_xdg_client_t client{harness.socket_name()};
    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_required_globals() && client.has_touch();
    }));

    client.create_toplevel("touch threshold test", "org.wayfire.TouchThresholdTest");
    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_pending_configure();
    }));

    client.attach_and_commit(200, 120);
    REQUIRE(harness.run_until([&] () { return mapped.size() == 1; }));

    auto view = wf::toplevel_cast(mapped.front());
    REQUIRE(view != nullptr);
    auto geometry = view->get_geometry();
    const double x = geometry.x + 20.0;
    const double y = geometry.y + 20.0;

    harness.touch_down(0, x, y);
    flush_touch(harness, client);
    harness.touch_down(1, x + 30.0, y);
    flush_touch(harness, client);
    harness.touch_down(2, x + 60.0, y);
    flush_touch(harness, client);

    REQUIRE(client.touch_events().size() == 6);
    CHECK(client.touch_events()[0].type == wf::test::touch_event_t::DOWN);
    CHECK(client.touch_events()[0].id == 0);
    CHECK(client.touch_events()[2].type == wf::test::touch_event_t::DOWN);
    CHECK(client.touch_events()[2].id == 1);
    CHECK(client.touch_events()[4].type == wf::test::touch_event_t::DOWN);
    CHECK(client.touch_events()[4].id == 2);

    harness.touch_down(3, x + 90.0, y);
    flush_touch(harness, client);

    REQUIRE(client.touch_events().size() == 7);
    CHECK(client.touch_events()[6].type == wf::test::touch_event_t::CANCEL);
}
