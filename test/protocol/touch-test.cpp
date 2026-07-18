#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/core.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/touch/touch.hpp>
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

static wayfire_toplevel_view map_touch_client(wf::test::headless_core_harness_t& harness,
    wf::test::wayland_xdg_client_t& client, const std::string& title)
{
    std::vector<wayfire_view> mapped;
    wf::signal::connection_t<wf::view_mapped_signal> on_map = [&] (wf::view_mapped_signal *ev)
    {
        mapped.push_back(ev->view);
    };
    wf::get_core().connect(&on_map);

    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_required_globals() && client.has_touch();
    }));

    client.create_toplevel(title, "org.wayfire.TouchTest");
    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_pending_configure();
    }));

    client.attach_and_commit(200, 120);
    REQUIRE(harness.run_until([&] () { return mapped.size() == 1; }));

    auto view = wf::toplevel_cast(mapped.front());
    REQUIRE(view != nullptr);
    view->move(0, 0);
    return view;
}

static void add_edge_swipe_binding(const std::string& binding_description,
    wf::activator_callback& callback)
{
    auto binding = wf::create_option_string<wf::activatorbinding_t>(binding_description);
    wf::get_core().bindings->add_activator(binding, &callback);
}
}

TEST_CASE("configured gesture finger count controls touch cancel threshold")
{
    wf::test::headless_core_harness_t harness{
        "[input]\n"
        "gesture_finger_count = 4\n"};
    harness.enable_touch_input();

    wf::test::wayland_xdg_client_t client{harness.socket_name()};
    auto view     = map_touch_client(harness, client, "touch threshold test");
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

    client.clear_touch_events();
    harness.touch_up(3);
    flush_touch(harness, client);
    harness.touch_up(2);
    flush_touch(harness, client);
    harness.touch_up(1);
    flush_touch(harness, client);
    harness.touch_up(0);
    flush_touch(harness, client);
    CHECK(client.touch_events().empty());
}

TEST_CASE("edge swipe bindings preempt only matching screen edges")
{
    wf::test::headless_core_harness_t harness;
    harness.enable_touch_input();

    wf::test::wayland_xdg_client_t client{harness.socket_name()};
    auto view     = map_touch_client(harness, client, "edge swipe test");
    auto geometry = view->get_geometry();

    wf::activator_callback non_matching_callback = [] (const wf::activator_data_t&) { return true; };
    add_edge_swipe_binding("edge-swipe left 3", non_matching_callback);

    harness.touch_down(0, geometry.x + 20.0, geometry.y + 1.0);
    flush_touch(harness, client);

    REQUIRE(client.touch_events().size() == 2);
    CHECK(client.touch_events()[0].type == wf::test::touch_event_t::DOWN);
    CHECK(client.touch_events()[1].type == wf::test::touch_event_t::FRAME);

    harness.touch_up(0);
    flush_touch(harness, client);
    client.clear_touch_events();

    wf::activator_callback matching_callback = [] (const wf::activator_data_t&) { return true; };
    add_edge_swipe_binding("edge-swipe down 3", matching_callback);

    harness.touch_down(0, geometry.x + 20.0, geometry.y + 1.0);
    flush_touch(harness, client);
    harness.touch_motion(0, geometry.x + 20.0, geometry.y + 30.0);
    flush_touch(harness, client);
    harness.touch_up(0);
    flush_touch(harness, client);

    CHECK(client.touch_events().empty());

    wf::get_core().bindings->rem_binding(&matching_callback);
    wf::get_core().bindings->rem_binding(&non_matching_callback);
}

TEST_CASE("custom touch gestures can consume client touch sequences")
{
    wf::test::headless_core_harness_t harness;
    harness.enable_touch_input();

    wf::test::wayland_xdg_client_t client{harness.socket_name()};
    auto view     = map_touch_client(harness, client, "custom gesture consume test");
    auto geometry = view->get_geometry();

    std::vector<std::unique_ptr<wf::touch::gesture_action_t>> actions;
    actions.emplace_back(std::make_unique<wf::touch::touch_action_t>(2, true));
    wf::touch::gesture_t custom_gesture{std::move(actions), [] () {}, [] () {},
        [] (const wf::touch::gesture_state_t& state, const wf::touch::gesture_event_t&)
        {
            return state.fingers.size() == 2;
        }
    };
    wf::get_core().add_touch_gesture(&custom_gesture);

    harness.touch_down(0, geometry.x + 20.0, geometry.y + 20.0);
    flush_touch(harness, client);
    harness.touch_down(1, geometry.x + 50.0, geometry.y + 20.0);
    flush_touch(harness, client);

    REQUIRE(client.touch_events().size() == 3);
    CHECK(client.touch_events()[0].type == wf::test::touch_event_t::DOWN);
    CHECK(client.touch_events()[1].type == wf::test::touch_event_t::FRAME);
    CHECK(client.touch_events()[2].type == wf::test::touch_event_t::CANCEL);

    client.clear_touch_events();
    harness.touch_up(1);
    flush_touch(harness, client);
    harness.touch_up(0);
    flush_touch(harness, client);
    CHECK(client.touch_events().empty());

    wf::get_core().rem_touch_gesture(&custom_gesture);
}
