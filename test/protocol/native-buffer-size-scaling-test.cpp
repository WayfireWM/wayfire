#include "wayfire/img.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-transform.hpp>

#include <algorithm>
#include <vector>

#include "../support/headless-core-harness.hpp"
#include "../support/wayland-xdg-client.hpp"

namespace
{
class logical_size_transformer_t : public wf::scene::transformer_base_node_t
{
  public:
    logical_size_transformer_t() : transformer_base_node_t(false)
    {}

    std::string stringify() const override
    {
        return "logical-size-test-transformer";
    }

    class render_instance_t :
        public wf::scene::transformer_render_instance_t<logical_size_transformer_t>
    {
      public:
        using transformer_render_instance_t::transformer_render_instance_t;

        void render(const wf::scene::render_instruction_t& data) override
        {
            wf::dimensionsf_t logical_size;
            auto texture  = get_texture(data.target.scale, &logical_size);
            auto geometry = wf::construct_box(
                wf::origin(self->get_children_bounding_box()), logical_size);
            geometry = data.target.aligned_geometry_from_geometry_box(geometry);
            data.pass->add_texture(texture, data.target, geometry, data.damage);
        }
    };

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<render_instance_t>(this, push_damage, shown_on));
    }
};
}

TEST_CASE("fractional-scale rounded client buffers render pixel-aligned")
{
    wf::test::headless_core_harness_t harness{
        "[core]\n"
        "background_color = 0 0 0 1\n"
        "[workarounds]\n"
        "use_native_buffer_size = true\n"
    };
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

    client.create_toplevel("rounded fractional scale test", "org.wayfire.RoundedFractionalScaleTest");
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
    const int view_x = 300;
    const int view_y = 200;
    view->move(view_x, view_y);

    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return client.last_fractional_scale().has_value();
    }));

    REQUIRE(client.last_fractional_scale().has_value());
    CHECK(client.last_fractional_scale().value() == 239);

    const int logical_width = 300;
    const int logical_height = 30;
    const int buffer_width = 598;
    const int buffer_height = 60;
    const uint32_t red = 0xff0000ffu;
    const uint32_t green = 0x00ff00ffu;
    std::vector<uint32_t> pixels(buffer_width * buffer_height);
    for (int y = 0; y < buffer_height; ++y)
    {
        for (int x = 0; x < buffer_width; ++x)
        {
            pixels[y * buffer_width + x] = ((x + y) % 2) ? green : red;
        }
    }

    client.attach_with_fractional_scale(logical_width, logical_height, pixels);
    REQUIRE(harness.run_until([&]
    {
        client.dispatch_once();
        return view->get_geometry().x == view_x &&
        view->get_geometry().y == view_y &&
        view->get_geometry().width == logical_width &&
        view->get_geometry().height == logical_height;
    }));

    SUBCASE("direct surface path")
    {
        CHECK(view->get_transformed_node()->get_transformer("logical-size-test-transformer") == nullptr);
    }

    SUBCASE("transformed zero-copy path")
    {
        auto transformer = std::make_shared<logical_size_transformer_t>();
        view->get_transformed_node()->add_transformer(transformer, wf::TRANSFORMER_HIGHLEVEL,
            "logical-size-test-transformer");
    }

    auto output_pixels = harness.capture_output_pixels();
    const int output_width = output->handle->width;
    const int output_height = output->handle->height;

    int min_x = output_width;
    int min_y = output_height;
    int max_x = -1;
    int max_y = -1;
    int mixed_pixels = 0;
    const auto is_background = [] (uint32_t pixel)
    {
        return (pixel & 0xffffff00u) == 0;
    };

    image_io::write_to_file("/tmp/pixels.png",
        (uint8_t*)output_pixels.data(), output_width, output_height, "png", false);

    for (int y = 0; y < output_height; ++y)
    {
        for (int x = 0; x < output_width; ++x)
        {
            auto pixel = output_pixels[y * output_width + x];
            if ((pixel == red) || (pixel == green))
            {
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
            }

            if (!is_background(pixel) && (pixel != red) && (pixel != green))
            {
                mixed_pixels++;
            }
        }
    }

    REQUIRE(max_x >= min_x);
    REQUIRE(max_y >= min_y);
    const int expected_x = static_cast<int>(std::floor(view_x * output->get_scale()));
    const int expected_y = static_cast<int>(std::floor(view_y * output->get_scale()));
    const int bounding_width = static_cast<int>(std::ceil(
        (view_x + logical_width) * output->get_scale())) - expected_x;
    const int bounding_height = static_cast<int>(std::ceil(
        (view_y + logical_height) * output->get_scale())) - expected_y;

    CHECK(min_x == expected_x);
    CHECK(min_y == expected_y);
    CHECK(max_x - min_x + 1 == std::min(buffer_width, bounding_width));
    CHECK(max_y - min_y + 1 == std::min(buffer_height, bounding_height));
    CHECK(is_background(output_pixels[expected_y * output_width + expected_x - 1]));
    CHECK(is_background(output_pixels[(expected_y - 1) * output_width + expected_x]));
    CHECK(is_background(output_pixels[expected_y * output_width + max_x + 1]));
    CHECK(is_background(output_pixels[(max_y + 1) * output_width + expected_x]));
    CHECK(mixed_pixels == 0);
}
