#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/region.hpp>
#include <wayfire/render.hpp>

#include <algorithm>
#include <tuple>
#include <vector>

namespace
{
std::vector<wlr_box> as_boxes(const wf::region_t& region)
{
    std::vector<wlr_box> boxes;
    for (auto it = region.begin(); it != region.end(); ++it)
    {
        boxes.push_back(wlr_box_from_pixman_box(*it));
    }

    std::sort(boxes.begin(), boxes.end(), [] (const auto& a, const auto& b)
    {
        return std::tie(a.x, a.y, a.width, a.height) <
        std::tie(b.x, b.y, b.width, b.height);
    });

    return boxes;
}

std::vector<wf::geometry_t> as_boxes(const wf::regionf_t& region)
{
    std::vector<wf::geometry_t> boxes;
    for (auto it = region.begin(); it != region.end(); ++it)
    {
        boxes.push_back({it->x1, it->y1, it->x2 - it->x1, it->y2 - it->y1});
    }

    std::sort(boxes.begin(), boxes.end(), [] (const auto& a, const auto& b)
    {
        return std::tie(a.x, a.y, a.width, a.height) <
        std::tie(b.x, b.y, b.width, b.height);
    });

    return boxes;
}
}

TEST_CASE("region supports copy move and clear semantics")
{
    wf::region_t original{wf::geometry_t{0, 0, 10, 10}};
    wf::region_t copy = original;
    copy += wf::point_t{5, 0};

    REQUIRE(original.contains_point({1, 1}));
    REQUIRE_FALSE(original.contains_point({11, 1}));
    REQUIRE(copy.contains_point({6, 1}));
    REQUIRE_FALSE(copy.contains_point({1, 1}));

    wf::region_t moved = std::move(copy);
    auto moved_boxes   = as_boxes(moved);
    REQUIRE(moved_boxes.size() == 1);
    REQUIRE(moved_boxes[0].x == 5);
    REQUIRE(moved_boxes[0].y == 0);
    REQUIRE(moved_boxes[0].width == 10);
    REQUIRE(moved_boxes[0].height == 10);
    REQUIRE(copy.empty());

    moved.clear();
    REQUIRE(moved.empty());
}

TEST_CASE("region set operations preserve expected coverage")
{
    wf::region_t region{wf::geometry_t{0, 0, 10, 10}};

    auto intersection = region & wlr_box{5, 5, 10, 10};
    auto intersection_boxes = as_boxes(intersection);
    REQUIRE(intersection_boxes.size() == 1);
    REQUIRE(intersection_boxes[0].x == 5);
    REQUIRE(intersection_boxes[0].y == 5);
    REQUIRE(intersection_boxes[0].width == 5);
    REQUIRE(intersection_boxes[0].height == 5);

    auto united = region | wlr_box{10, 0, 5, 10};
    auto united_boxes = as_boxes(united);
    REQUIRE(united_boxes.size() == 1);
    REQUIRE(united_boxes[0].x == 0);
    REQUIRE(united_boxes[0].y == 0);
    REQUIRE(united_boxes[0].width == 15);
    REQUIRE(united_boxes[0].height == 10);

    auto subtracted = region ^ wlr_box{2, 2, 6, 6};
    REQUIRE(subtracted.contains_point({1, 1}));
    REQUIRE(subtracted.contains_point({9, 9}));
    REQUIRE_FALSE(subtracted.contains_point({5, 5}));
    auto extents = subtracted.get_extents();
    REQUIRE(extents.x1 == 0);
    REQUIRE(extents.y1 == 0);
    REQUIRE(extents.x2 == 10);
    REQUIRE(extents.y2 == 10);
}

TEST_CASE("region translation scaling and float containment work together")
{
    wf::region_t region{wf::geometry_t{1, 2, 3, 4}};
    auto shifted = region + wf::point_t{2, 3};
    auto scaled  = shifted * 2.0f;

    auto shifted_boxes = as_boxes(shifted);
    REQUIRE(shifted_boxes.size() == 1);
    REQUIRE(shifted_boxes[0].x == 3);
    REQUIRE(shifted_boxes[0].y == 5);
    REQUIRE(shifted_boxes[0].width == 3);
    REQUIRE(shifted_boxes[0].height == 4);

    auto scaled_boxes = as_boxes(scaled);
    REQUIRE(scaled_boxes.size() == 1);
    REQUIRE(scaled_boxes[0].x == 6);
    REQUIRE(scaled_boxes[0].y == 10);
    REQUIRE(scaled_boxes[0].width == 6);
    REQUIRE(scaled_boxes[0].height == 8);
    REQUIRE(scaled.contains_pointf({6.5, 10.5}));
    REQUIRE_FALSE(scaled.contains_pointf({12.5, 18.5}));
}

TEST_CASE("region edge expansion handles no-op growth and shrink")
{
    wf::region_t region{wf::geometry_t{0, 0, 10, 10}};

    region.expand_edges(0);
    auto boxes0 = as_boxes(region);
    REQUIRE(boxes0.size() == 1);
    REQUIRE(boxes0[0].x == 0);
    REQUIRE(boxes0[0].y == 0);
    REQUIRE(boxes0[0].width == 10);
    REQUIRE(boxes0[0].height == 10);

    region.expand_edges(2);
    auto boxes1 = as_boxes(region);
    REQUIRE(boxes1.size() == 1);
    REQUIRE(boxes1[0].x == -2);
    REQUIRE(boxes1[0].y == -2);
    REQUIRE(boxes1[0].width == 14);
    REQUIRE(boxes1[0].height == 14);

    region.expand_edges(-3);
    auto boxes2 = as_boxes(region);
    REQUIRE(boxes2.size() == 1);
    REQUIRE(boxes2[0].x == 1);
    REQUIRE(boxes2[0].y == 1);
    REQUIRE(boxes2[0].width == 8);
    REQUIRE(boxes2[0].height == 8);
}

TEST_CASE("floating region supports logical geometry operations")
{
    wf::regionf_t region{{1.25, 2.5, 3.5, 4.25}};
    auto shifted = region + wf::pointf_t{2, 3};
    auto scaled  = shifted * 2.0;

    auto shifted_boxes = as_boxes(shifted);
    REQUIRE(shifted_boxes == std::vector<wf::geometry_t>{{3.25, 5.5, 3.5, 4.25}});

    auto scaled_boxes = as_boxes(scaled);
    REQUIRE(scaled_boxes == std::vector<wf::geometry_t>{{6.5, 11.0, 7.0, 8.5}});
    REQUIRE(scaled.contains_pointf({6.5, 11.0}));
    REQUIRE(scaled.contains_pointf({13.25, 19.25}));
    REQUIRE_FALSE(scaled.contains_pointf({13.75, 19.75}));
}

TEST_CASE("floating region preserves coordinates after reversed translations")
{
    const wf::geometry_t original_box{0.2, 0.3, 100.4, 200.5};
    wf::regionf_t region{original_box};

    region += wf::pointf_t{1920.1, 1080.1};
    region -= wf::pointf_t{1920.1, 1080.1};

    REQUIRE(as_boxes(region) == std::vector<wf::geometry_t>{original_box});

    region -= wf::pointf_t{1920.1, 1080.1};
    region -= wf::pointf_t{-743.3, 512.7};
    region += wf::pointf_t{-743.3, 512.7};
    region += wf::pointf_t{1920.1, 1080.1};

    REQUIRE(as_boxes(region) == std::vector<wf::geometry_t>{original_box});
}

TEST_CASE("floating region set operations respect pending translations")
{
    wf::regionf_t region{{0.25, 0.5, 10.0, 10.0}};
    region += wf::pointf_t{100.5, 200.25};

    auto intersection = region & wf::geometry_t{105.75, 205.75, 10.0, 10.0};
    REQUIRE(as_boxes(intersection) ==
        std::vector<wf::geometry_t>{{105.75, 205.75, 5.0, 5.0}});

    region ^= wf::geometry_t{100.75, 200.75, 5.0, 10.0};
    REQUIRE_FALSE(region.contains_pointf({101.0, 201.0}));
    REQUIRE(region.contains_pointf({106.0, 201.0}));
}

TEST_CASE("floating region operations account for independent translations")
{
    wf::regionf_t left{{0.25, 0.5, 10.0, 10.0}};
    wf::regionf_t right{{5.75, 5.25, 10.0, 10.0}};
    left  += wf::pointf_t{100.5, 200.25};
    right += wf::pointf_t{100.0, 200.5};

    auto intersection = left & right;
    REQUIRE(as_boxes(intersection) ==
        std::vector<wf::geometry_t>{{105.75, 205.75, 5.0, 5.0}});

    auto united = left | right;
    REQUIRE(united.contains_pointf({101.0, 201.0}));
    REQUIRE(united.contains_pointf({115.0, 215.0}));

    left ^= right;
    left -= wf::pointf_t{100.5, 200.25};
    REQUIRE(left.contains_pointf({1.0, 1.0}));
    REQUIRE_FALSE(left.contains_pointf({6.0, 6.0}));
}

TEST_CASE("floating region raw access exposes translated coordinates")
{
    wf::regionf_t region{{0.25, 0.5, 10.0, 20.0}};
    region += wf::pointf_t{100.5, 200.25};

    auto *raw    = region.to_pixman();
    auto extents = *pixman_region64f_extents(raw);
    REQUIRE(extents.x1 == 100.75);
    REQUIRE(extents.y1 == 200.75);
    REQUIRE(extents.x2 == 110.75);
    REQUIRE(extents.y2 == 220.75);
}

TEST_CASE("framebuffer region conversion tolerates rounding near integer edges")
{
    wf::render_buffer_t buffer{nullptr, {100, 100}};
    wf::render_target_t target{buffer};
    target.geometry = {0, 0, 100, 100};

    wf::regionf_t region{{-9.9998, 2.25, 4.9998, 3.5}};
    auto boxes = as_boxes(target.framebuffer_region_from_geometry_region(region));

    REQUIRE(boxes.size() == 1);
    REQUIRE(boxes[0].x == -10);
    REQUIRE(boxes[0].y == 2);
    REQUIRE(boxes[0].width == 5);
    REQUIRE(boxes[0].height == 4);

    auto box = target.framebuffer_box_from_geometry_box({-9.9998, 2.25, 4.9998, 3.5});
    REQUIRE(box.x == -10);
    REQUIRE(box.y == 2);
    REQUIRE(box.width == 5);
    REQUIRE(box.height == 4);

    box = target.framebuffer_texture_dst_box_from_geometry_box({-9.9998, 2.25, 4.9998, 3.5});
    REQUIRE(box.x == -10);
    REQUIRE(box.y == 2);
    REQUIRE(box.width == 5);
    REQUIRE(box.height == 4);

    wf::regionf_t fractional_edge{{0, 0, 5.02, 1}};
    boxes = as_boxes(target.framebuffer_region_from_geometry_region(fractional_edge));
    REQUIRE(boxes.size() == 1);
    REQUIRE(boxes[0].width == 6);
}

TEST_CASE("framebuffer region conversion preserves target transforms")
{
    wf::render_buffer_t buffer{nullptr, {100, 200}};
    wf::render_target_t target{buffer};
    target.geometry = {0, 0, 100, 200};
    wf::geometry_t box{10.25, 20.25, 5.5, 10.5};

    for (int transform = WL_OUTPUT_TRANSFORM_NORMAL;
         transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270; transform++)
    {
        CAPTURE(transform);
        target.wl_transform = (wl_output_transform)transform;
        auto expected = target.framebuffer_box_from_geometry_box(box);
        auto boxes    = as_boxes(target.framebuffer_region_from_geometry_region(wf::regionf_t{box}));

        REQUIRE(boxes.size() == 1);
        REQUIRE(boxes[0].x == expected.x);
        REQUIRE(boxes[0].y == expected.y);
        REQUIRE(boxes[0].width == expected.width);
        REQUIRE(boxes[0].height == expected.height);
    }

    target.wl_transform = WL_OUTPUT_TRANSFORM_NORMAL;
    target.subbuffer    = wf::geometry_t{5, 10, 50, 100};
    auto expected = target.framebuffer_box_from_geometry_box(box);
    auto boxes    = as_boxes(target.framebuffer_region_from_geometry_region(wf::regionf_t{box}));
    REQUIRE(boxes.size() == 1);
    REQUIRE(boxes[0].x == expected.x);
    REQUIRE(boxes[0].y == expected.y);
    REQUIRE(boxes[0].width == expected.width);
    REQUIRE(boxes[0].height == expected.height);
}

TEST_CASE("geometry to integer box conversion contains fractional extents")
{
    auto box = wf::containing_box(wf::geometry_t{0.9, 1.1, 10.2, 5.8});
    REQUIRE(box.x == 0);
    REQUIRE(box.y == 1);
    REQUIRE(box.width == 12);
    REQUIRE(box.height == 6);

    wf::region_t region;
    region |= wf::geometry_t{0.9, 1.1, 10.2, 5.8};
    auto boxes = as_boxes(region);
    REQUIRE(boxes.size() == 1);
    REQUIRE(boxes[0].x == 0);
    REQUIRE(boxes[0].y == 1);
    REQUIRE(boxes[0].width == 12);
    REQUIRE(boxes[0].height == 6);
}
