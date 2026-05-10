#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>

#include <vector>

#include "../support/headless-core-harness.hpp"
#include "../support/wayland-xdg-client.hpp"

TEST_CASE("xdg toplevel maps after configure ack and buffer commit")
{
    wf::test::headless_core_harness_t harness;

    std::vector<wayfire_view> mapped;
    std::vector<wayfire_view> unmapped;
    wf::signal::connection_t<wf::view_mapped_signal> on_map = [&] (wf::view_mapped_signal *ev)
    {
        mapped.push_back(ev->view);
    };
    wf::signal::connection_t<wf::view_unmapped_signal> on_unmap = [&] (wf::view_unmapped_signal *ev)
    {
        unmapped.push_back(ev->view);
    };

    wf::get_core().connect(&on_map);
    wf::get_core().connect(&on_unmap);

    wf::test::wayland_xdg_client_t client{harness.socket_name()};
    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_required_globals();
    }));

    client.create_toplevel("xdg-shell test", "org.wayfire.Test");

    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_pending_configure();
    }));
    REQUIRE(client.last_configure_serial() != 0);
    CHECK(mapped.empty());

    client.attach_and_commit(200, 120);
    REQUIRE(harness.run_until([&] () { return mapped.size() == 1; }));
    REQUIRE(mapped.front());
    CHECK(mapped.front()->get_title() == "xdg-shell test");
    CHECK(mapped.front()->get_app_id() == "org.wayfire.Test");
    CHECK(mapped.front()->get_output() == harness.output());
    CHECK(mapped.front()->is_mapped());

    client.destroy_toplevel();
    REQUIRE(harness.run_until([&] () { return unmapped.size() == 1; }));
    CHECK(unmapped.front() == mapped.front());
}

TEST_CASE("fullscreen view matches exact logical output size at fractional scale")
{
    wf::test::headless_core_harness_t harness;
    auto *output = harness.output();
    REQUIRE(output != nullptr);

    auto config = wf::get_core().output_layout->get_current_configuration();
    REQUIRE(config.count(output->handle) == 1);

    config.at(output->handle).scale = 1.99;
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

    client.create_toplevel("fractional fullscreen", "org.wayfire.FractionalFullscreen");
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

    client.clear_pending_configure();

    wf::get_core().default_wm->fullscreen_request(view, output, true);
    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_pending_configure() && client.last_fractional_scale().has_value() &&
        client.last_toplevel_configure_fullscreen() && client.last_toplevel_size().has_value() &&
        (client.last_toplevel_size()->first > 0) && (client.last_toplevel_size()->second > 0);
    }));

    REQUIRE(client.last_fractional_scale().has_value());
    const uint32_t fractional_scale = client.last_fractional_scale().value();
    const int preferred_scale = client.last_preferred_buffer_scale();
    const auto configured_size = client.last_toplevel_size().value();

    CHECK(preferred_scale == 1);
    CHECK(fractional_scale == 239);
    CHECK(configured_size.first == 643);
    CHECK(configured_size.second == 362);

    client.attach_with_fractional_scale(configured_size.first, configured_size.second);
    const auto committed_size = client.last_committed_buffer_size();

    CHECK(committed_size.first == output->handle->width);
    CHECK(committed_size.second == output->handle->height);
}
