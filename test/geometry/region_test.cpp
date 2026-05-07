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
}

TEST_CASE("region supports copy move and clear semantics")
{
    wf::region_t original{{0, 0, 10, 10}};
    wf::region_t copy = original;
    copy += wf::point_t{5, 0};

    REQUIRE(original.contains_point({1, 1}));
    REQUIRE_FALSE(original.contains_point({11, 1}));
    REQUIRE(copy.contains_point({6, 1}));
    REQUIRE_FALSE(copy.contains_point({1, 1}));

    wf::region_t moved = std::move(copy);
    auto moved_boxes   = as_boxes(moved);
    REQUIRE(moved_boxes.size() == 1);
    REQUIRE(moved_boxes[0] == wf::geometry_t{5, 0, 10, 10});
    REQUIRE(copy.empty());

    moved.clear();
    REQUIRE(moved.empty());
}

TEST_CASE("region set operations preserve expected coverage")
{
    wf::region_t region{{0, 0, 10, 10}};

    auto intersection = region & wlr_box{5, 5, 10, 10};
    REQUIRE(as_boxes(intersection) == std::vector<wlr_box>{{5, 5, 5, 5}});

    auto united = region | wlr_box{10, 0, 5, 10};
    REQUIRE(as_boxes(united) == std::vector<wlr_box>{{0, 0, 15, 10}});

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
    wf::region_t region{{1, 2, 3, 4}};
    auto shifted = region + wf::point_t{2, 3};
    auto scaled  = shifted * 2.0f;

    REQUIRE(as_boxes(shifted) == std::vector<wlr_box>{{3, 5, 3, 4}});
    REQUIRE(as_boxes(scaled) == std::vector<wlr_box>{{6, 10, 6, 8}});
    REQUIRE(scaled.contains_pointf({6.5, 10.5}));
    REQUIRE_FALSE(scaled.contains_pointf({12.5, 18.5}));
}

TEST_CASE("region edge expansion handles no-op growth and shrink")
{
    wf::region_t region{{0, 0, 10, 10}};

    region.expand_edges(0);
    REQUIRE(as_boxes(region) == std::vector<wlr_box>{{0, 0, 10, 10}});

    region.expand_edges(2);
    REQUIRE(as_boxes(region) == std::vector<wlr_box>{{-2, -2, 14, 14}});

    region.expand_edges(-3);
    REQUIRE(as_boxes(region) == std::vector<wlr_box>{{1, 1, 8, 8}});
}
