#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/core.hpp>
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
