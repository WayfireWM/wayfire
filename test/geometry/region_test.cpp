#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/region.hpp>

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
