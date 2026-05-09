#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/signal-definitions.hpp>

#include <vector>

#include "../support/headless-core-harness.hpp"
#include "../support/wayland-layer-shell-client.hpp"
#include "../support/wayland-xdg-client.hpp"

namespace
{
constexpr uint32_t LAYER_OVERLAY = 3;
constexpr uint32_t LAYER_KEYBOARD_NONE = 0;
constexpr uint32_t LAYER_ANCHOR_BOTTOM = 2;
constexpr uint32_t LAYER_ANCHOR_RIGHT  = 8;
}

TEST_CASE("layer-shell popup with keyboard none does not steal focus")
{
    wf::test::headless_core_harness_t harness;

    std::vector<wayfire_view> mapped;
    wf::signal::connection_t<wf::view_mapped_signal> on_map = [&] (wf::view_mapped_signal *ev)
    {
        mapped.push_back(ev->view);
    };

    wf::get_core().connect(&on_map);

    wf::test::wayland_xdg_client_t focused_client{harness.socket_name()};
    wf::test::wayland_layer_shell_client_t layer_client{harness.socket_name()};
    REQUIRE(harness.run_until([&]
    {
        focused_client.dispatch_once();
        layer_client.dispatch_once();
        return focused_client.has_required_globals() && layer_client.has_required_globals();
    }));

    focused_client.create_toplevel("focus target", "org.wayfire.FocusTarget");
    REQUIRE(harness.run_until([&]
    {
        focused_client.dispatch_once();
        return focused_client.has_pending_configure();
    }));

    focused_client.attach_and_commit(200, 120);
    REQUIRE(harness.run_until([&]
    {
        focused_client.dispatch_once();
        return mapped.size() == 1;
    }));

    auto focused_view = mapped.front();
    REQUIRE(focused_view);
    REQUIRE(harness.run_until([&]
    {
        return wf::get_core().seat->get_active_view() == focused_view;
    }));

    layer_client.create_layer_surface("regression-layer-shell-popup",
        LAYER_OVERLAY,
        LAYER_KEYBOARD_NONE,
        120, 40,
        LAYER_ANCHOR_BOTTOM | LAYER_ANCHOR_RIGHT);
    REQUIRE(harness.run_until([&]
    {
        layer_client.dispatch_once();
        return layer_client.has_pending_layer_configure();
    }));

    layer_client.attach_layer_and_commit(120, 40);
    REQUIRE(harness.run_until([&]
    {
        layer_client.dispatch_once();
        return mapped.size() == 2;
    }));

    CHECK(wf::get_core().seat->get_active_view() == focused_view);

    layer_client.create_popup(140, 32, layer_client.last_layer_configure_serial());
    REQUIRE(harness.run_until([&]
    {
        layer_client.dispatch_once();
        return layer_client.has_pending_popup_configure();
    }));

    layer_client.attach_popup_and_commit(140, 32);
    REQUIRE(harness.run_until([&]
    {
        layer_client.dispatch_once();
        return mapped.size() == 3;
    }));

    CHECK(wf::get_core().seat->get_active_view() == focused_view);
}
