#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <config.h>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <xcb/xcb.h>

#include <wayfire/core.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>

#include "../support/headless-core-harness.hpp"

namespace
{
class xwayland_test_client_t
{
  public:
    xwayland_test_client_t(const std::string& display, int width, int height)
    {
        initial_width  = width;
        initial_height = height;
        connection     = xcb_connect(display.c_str(), nullptr);
        if (xcb_connection_has_error(connection))
        {
            throw std::runtime_error("Failed to connect test client to Xwayland display");
        }

        auto *setup = xcb_get_setup(connection);
        screen = xcb_setup_roots_iterator(setup).data;
        window = xcb_generate_id(connection);
        gc     = xcb_generate_id(connection);

        uint32_t values[] = {
            screen->black_pixel,
            XCB_EVENT_MASK_STRUCTURE_NOTIFY,
        };
        xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root,
            0, 0, width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
            screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);

        uint32_t gc_values[] = {screen->white_pixel};
        xcb_create_gc(connection, gc, window, XCB_GC_FOREGROUND, gc_values);

        const std::string title = "xwayland transaction timeout test";
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
            XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, title.size(), title.c_str());
        flush();
    }

    ~xwayland_test_client_t()
    {
        if (connection)
        {
            xcb_free_gc(connection, gc);
            xcb_destroy_window(connection, window);
            xcb_flush(connection);
            xcb_disconnect(connection);
        }
    }

    void map()
    {
        xcb_map_window(connection, window);
        draw(initial_width, initial_height);
        flush();
    }

    void resize(int width, int height)
    {
        uint32_t values[] = {uint32_t(width), uint32_t(height)};
        xcb_configure_window(connection, window,
            XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
        draw(width, height);
        flush();
    }

    void draw(int width, int height)
    {
        xcb_rectangle_t rect{0, 0, uint16_t(width), uint16_t(height)};
        xcb_poly_fill_rectangle(connection, window, gc, 1, &rect);
    }

    void flush()
    {
        xcb_flush(connection);
    }

  private:
    xcb_connection_t *connection = nullptr;
    xcb_screen_t *screen = nullptr;
    xcb_window_t window  = XCB_WINDOW_NONE;
    xcb_gcontext_t gc    = XCB_NONE;
    int initial_width    = 0;
    int initial_height   = 0;
};
}

TEST_CASE("xwayland resize with zero transaction timeout")
{
    using namespace std::chrono_literals;

    wf::test::headless_core_harness_t harness{
        "[core]\n"
        "xwayland = lazy\n"
        "transaction_timeout = 0\n"};

    REQUIRE(harness.run_until([]
    {
        return !wf::get_core().get_xwayland_display().empty();
    }));

    std::vector<wayfire_view> mapped;
    wf::signal::connection_t<wf::view_mapped_signal> on_map = [&] (wf::view_mapped_signal *ev)
    {
        mapped.push_back(ev->view);
    };
    wf::get_core().connect(&on_map);

    const auto display = wf::get_core().get_xwayland_display();
    auto client_future = std::async(std::launch::async, [display] ()
    {
        return std::make_unique<xwayland_test_client_t>(display, 200, 120);
    });

    REQUIRE(harness.run_until([&]
    {
        return client_future.wait_for(0ms) == std::future_status::ready;
    }, 500));

    auto client = client_future.get();
    harness.roundtrip();
    client->map();

    REQUIRE(harness.run_until([&]
    {
        client->flush();
        return mapped.size() == 1;
    }));

    auto view = wf::toplevel_cast(mapped.front());
    REQUIRE(view);
    REQUIRE(harness.run_until([&]
    {
        auto geometry = view->get_geometry();
        return (geometry.width == 200) && (geometry.height == 120);
    }));

    client->resize(320, 180);
    CHECK(harness.run_until([&]
    {
        client->flush();
        auto geometry = view->get_geometry();
        return (geometry.width == 320) && (geometry.height == 180);
    }));
}
