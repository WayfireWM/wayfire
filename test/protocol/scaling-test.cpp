#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>

#include <algorithm>
#include <sstream>
#include <set>
#include <vector>

#include "../support/headless-core-harness.hpp"
#include "../support/wayland-xdg-client.hpp"

TEST_CASE("fractional-scale render stays pixel-aligned away from origin")
{
    wf::test::headless_core_harness_t harness;
    auto *output = harness.output();
    REQUIRE(output != nullptr);

    auto config = wf::get_core().output_layout->get_current_configuration();
    REQUIRE(config.count(output->handle) == 1);

    config.at(output->handle).scale = 5.0 / 3.0;
    REQUIRE(wf::get_core().output_layout->apply_configuration(config));

    wf::test::wayland_xdg_client_t client{harness.socket_name()};
    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_required_globals();
    }));

    std::vector<wayfire_view> mapped;
    wf::signal::connection_t<wf::view_mapped_signal> on_map = [&] (wf::view_mapped_signal *ev)
    {
        mapped.push_back(ev->view);
    };
    wf::get_core().connect(&on_map);

    client.create_toplevel("rendering test", "org.wayfire.RenderingTest");
    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_pending_configure();
    }));

    client.attach_and_commit(200, 120);
    REQUIRE(harness.run_until([&] () { return mapped.size() == 1; }));

    auto view = wf::toplevel_cast(mapped.front());
    REQUIRE(view != nullptr);
    REQUIRE(view->get_output() == output);

    view->move(1, 1);

    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.last_fractional_scale().has_value();
    }));

    REQUIRE(client.last_fractional_scale().has_value());
    const uint32_t fractional_scale = client.last_fractional_scale().value();
    const int preferred_scale = client.last_preferred_buffer_scale();

    CHECK(preferred_scale == 1);
    CHECK(fractional_scale == 200);

    const int logical_width = 6;
    const int logical_height = 6;
    const int buffer_width = 10;
    const int buffer_height = 10;

    std::vector<uint32_t> pixels(buffer_width * buffer_height);
    for (int y = 0; y < buffer_height; ++y)
    {
        for (int x = 0; x < buffer_width; ++x)
        {
            pixels[y * buffer_width + x] = ((x + y) % 2) ? 0x00ff0000u : 0x0000ff00u;
        }
    }

    client.attach_with_fractional_scale(logical_width, logical_height, pixels);
    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return view->get_geometry().x == 1 && view->get_geometry().y == 1 &&
        view->get_geometry().width == logical_width &&
        view->get_geometry().height == logical_height;
    }));

    auto output_pixels = harness.capture_output_pixels();
    const int output_width = output->handle->width;
    const int output_height = output->handle->height;

    std::set<uint32_t> unique_pixels(output_pixels.begin(), output_pixels.end());

    int min_x = output_width;
    int min_y = output_height;
    int max_x = -1;
    int max_y = -1;
    int mixed_pixels = 0;

    const uint32_t red = 0xff0000ffu;
    const uint32_t green = 0xff00ff00u;
    const uint32_t background = 0xff000000u;

    INFO("unique pixel count: ", unique_pixels.size());
    for (auto pixel : unique_pixels)
    {
        std::ostringstream out;
        out << std::hex << pixel;
        INFO("pixel value: 0x", out.str());
    }

    for (int y = 0; y < output_height; ++y)
    {
        for (int x = 0; x < output_width; ++x)
        {
            auto pixel = output_pixels[y * output_width + x];
            if (pixel != background)
            {
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
            }

            if ((pixel != background) && (pixel != red) && (pixel != green))
            {
                mixed_pixels++;
            }
        }
    }

    REQUIRE(max_x >= min_x);
    REQUIRE(max_y >= min_y);
    CHECK(min_x == 1);
    CHECK(min_y == 1);
    CHECK(max_x - min_x + 1 == buffer_width);
    CHECK(max_y - min_y + 1 == buffer_height);
    CHECK(mixed_pixels == 0);
}
