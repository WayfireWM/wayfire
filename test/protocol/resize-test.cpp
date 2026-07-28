#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/core.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>

#include <string>
#include <vector>

#include <linux/input-event-codes.h>

#include "../support/headless-core-harness.hpp"
#include "../support/scoped-env.hpp"
#include "../support/wayland-xdg-client.hpp"

namespace
{
static wayfire_toplevel_view map_xdg_client(wf::test::headless_core_harness_t& harness,
    wf::test::wayland_xdg_client_t& client)
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
        return client.has_required_globals() && client.has_pointer();
    }));

    client.create_toplevel("resize test", "org.wayfire.ResizeTest");
    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_pending_configure();
    }));

    client.attach_and_commit(300, 300);
    REQUIRE(harness.run_until([&] () { return mapped.size() == 1; }));

    auto view = wf::toplevel_cast(mapped.front());
    REQUIRE(view != nullptr);
    return view;
}

static void apply_geometry(wf::test::headless_core_harness_t& harness,
    wf::test::wayland_xdg_client_t& client, wayfire_toplevel_view view,
    wf::geometry_t geometry)
{
    client.clear_pending_configure();
    view->set_geometry(geometry);

    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.has_pending_configure() && client.last_toplevel_size().has_value();
    }));

    auto size = client.last_toplevel_size().value();
    client.ack_last_configure();
    client.attach_and_commit(size.first ? size.first : geometry.width,
        size.second ? size.second : geometry.height);

    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return view->get_geometry() == geometry;
    }));
}

static void check_resize_case(wf::pointf_t start_ratio, wf::pointf_t delta,
    wf::geometry_t expected, const std::string& extra_config = "")
{
    wf::test::scoped_env_t plugin_path{"WAYFIRE_PLUGIN_PATH", TEST_PLUGIN_PATH};

    wf::test::headless_core_harness_t harness{
        "[core]\n"
        "plugins = resize\n" + extra_config,
        true};
    harness.enable_pointer_input();

    wf::test::wayland_xdg_client_t client{harness.socket_name()};
    auto view = map_xdg_client(harness, client);

    const wf::geometry_t base{100, 100, 300, 300};
    apply_geometry(harness, client, view, base);

    auto bbox = view->get_bounding_box();
    wf::pointf_t start{
        bbox.x + bbox.width * start_ratio.x,
        bbox.y + bbox.height * start_ratio.y,
    };

    client.clear_pending_configure();
    harness.pointer_motion(start.x, start.y);
    wf::get_core().default_wm->resize_request(view, 0);
    harness.pointer_motion(start.x + delta.x, start.y + delta.y);

    CHECK(view->toplevel()->pending().geometry == expected);
    harness.pointer_button(BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
}
}

TEST_CASE("interactive resize activation chooses edges from pointer zone")
{
    check_resize_case({0.05, 0.5}, {30, 40}, {130, 100, 270, 300});
    check_resize_case({0.5, 0.05}, {30, 40}, {100, 140, 300, 260});
    check_resize_case({0.95, 0.5}, {30, 40}, {100, 100, 330, 300});
    check_resize_case({0.5, 0.95}, {30, 40}, {100, 100, 300, 340});
    check_resize_case({0.05, 0.05}, {30, 40}, {130, 140, 270, 260});
    check_resize_case({0.95, 0.95}, {30, 40}, {100, 100, 330, 340});
    check_resize_case({0.5, 0.5}, {30, 40}, {100, 100, 330, 340});
    check_resize_case({0.5, 0.5}, {-30, -40}, {100, 100, 270, 260});
    check_resize_case({0.15, 0.05}, {30, 40}, {100, 140, 300, 260},
        "\n[resize]\n"
        "corner_threshold = 0.1\n");
}
