#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <vector>

#include <xcb/xcb.h>

#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>

#include "../../src/view/view-impl.hpp"
#include "../support/headless-core-harness.hpp"

namespace
{
struct xcb_disconnect_t
{
    void operator ()(xcb_connection_t *connection) const
    {
        xcb_disconnect(connection);
    }
};
}

TEST_CASE("decorated Xwayland toplevel does not shrink at fractional scale")
{
    setenv("WAYFIRE_PLUGIN_PATH", TEST_DECORATION_PLUGIN_DIR, 1);
    wf::test::headless_core_harness_t harness{
        "[core]\n"
        "plugins = decoration\n"
        "xwayland = true\n"
        "[workarounds]\n"
        "force_xwayland_scaling = true\n",
        true
    };

    auto *output = harness.output();
    REQUIRE(output != nullptr);
    auto config = wf::get_core().output_layout->get_current_configuration();
    REQUIRE(config.count(output->handle) == 1);
    config.at(output->handle).scale = 1.99;
    REQUIRE(wf::get_core().output_layout->apply_configuration(config));

    REQUIRE(harness.run_until([] () { return !wf::xwayland_get_display().empty(); }));
    const auto display = wf::xwayland_get_display();
    auto connect = std::async(std::launch::async, [&] ()
    {
        int screen_number = 0;
        auto *connection  = xcb_connect(display.c_str(), &screen_number);
        return std::pair{connection, screen_number};
    });
    REQUIRE(harness.run_until([&] ()
    {
        return connect.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    }));

    auto [raw_connection, screen_number] = connect.get();
    std::unique_ptr<xcb_connection_t, xcb_disconnect_t> connection{raw_connection};
    REQUIRE(connection != nullptr);
    REQUIRE(xcb_connection_has_error(connection.get()) == 0);

    auto screen = xcb_setup_roots_iterator(xcb_get_setup(connection.get()));
    for (int i = 0; i < screen_number; ++i)
    {
        xcb_screen_next(&screen);
    }

    REQUIRE(screen.data != nullptr);

    std::vector<wayfire_view> mapped;
    wf::signal::connection_t<wf::view_mapped_signal> on_map = [&] (wf::view_mapped_signal *ev)
    {
        mapped.push_back(ev->view);
    };
    wf::get_core().connect(&on_map);

    const auto window = xcb_generate_id(connection.get());
    const uint32_t values[] = {screen.data->black_pixel};
    xcb_create_window(connection.get(), XCB_COPY_FROM_PARENT, window, screen.data->root,
        100, 100, 600, 400, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen.data->root_visual, XCB_CW_BACK_PIXEL, values);
    xcb_map_window(connection.get(), window);
    xcb_flush(connection.get());

    REQUIRE(harness.run_until([&] () { return mapped.size() == 1; }));
    auto view = wf::toplevel_cast(mapped.front());
    REQUIRE(view != nullptr);
    REQUIRE(view->toplevel()->current().margins.left > 0);
    REQUIRE(view->toplevel()->current().margins.top > 0);

    const auto initial_geometry = view->get_geometry();
    for (int i = 0; i < 100; ++i)
    {
        harness.dispatch_once(10);
        while (auto *event = xcb_poll_for_event(connection.get()))
        {
            free(event);
        }
    }

    CHECK(view->get_geometry().width == initial_geometry.width);
    CHECK(view->get_geometry().height == initial_geometry.height);

    xcb_destroy_window(connection.get(), window);
    xcb_flush(connection.get());
}
