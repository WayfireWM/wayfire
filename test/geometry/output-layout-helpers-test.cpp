#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/config/types.hpp>

#include "../../src/core/output-layout-priv.hpp"

namespace
{
wlr_output_mode make_mode(int width, int height, int refresh)
{
    wlr_output_mode mode{};
    mode.width   = width;
    mode.height  = height;
    mode.refresh = refresh;
    return mode;
}

wf::output_state_t make_state(wf::output_image_source_t source,
    wf::output_config::position_t position, wlr_output_mode mode,
    double scale = 1.0,
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL)
{
    wf::output_state_t state;
    state.source   = source;
    state.position = position;
    state.mode     = mode;
    state.scale    = scale;
    state.transform = transform;
    return state;
}

wf::output_configuration_t config_from_states(const std::vector<wf::output_state_t>& states)
{
    wf::output_configuration_t config;
    uintptr_t id = 1;
    for (auto& state : states)
    {
        config.emplace(reinterpret_cast<wlr_output*>(id++), state);
    }

    return config;
}
}

TEST_CASE("output layout transform helpers round-trip known values")
{
    CHECK(wf::layout_detail::get_transform_from_string("normal") == WL_OUTPUT_TRANSFORM_NORMAL);
    CHECK(wf::layout_detail::get_transform_from_string("90_flipped") == WL_OUTPUT_TRANSFORM_FLIPPED_90);
    CHECK(wf::layout_detail::get_transform_from_string("invalid") == WL_OUTPUT_TRANSFORM_NORMAL);

    CHECK(wf::layout_detail::wl_transform_to_string(WL_OUTPUT_TRANSFORM_270) == "270");
    CHECK(wf::layout_detail::wl_transform_to_string(WL_OUTPUT_TRANSFORM_FLIPPED_180) == "180_flipped");
}

TEST_CASE("output layout modeline parser accepts valid forms and rejects invalid ones")
{
    drmModeModeInfo mode;
    REQUIRE(wf::layout_detail::parse_modeline(
        "148.50 1920 2008 2052 2200 1080 1084 1089 1125 +hsync +vsync", mode));

    CHECK(mode.hdisplay == 1920);
    CHECK(mode.vdisplay == 1080);
    CHECK((mode.flags & DRM_MODE_FLAG_PHSYNC) != 0);
    CHECK((mode.flags & DRM_MODE_FLAG_PVSYNC) != 0);
    CHECK(std::string{mode.name} == "1920x1080@60");

    REQUIRE(wf::layout_detail::parse_modeline(
        "74.25 1280 1390 1430 1650 720 725 730 750 -hsync -vsync interlace", mode));
    CHECK((mode.flags & DRM_MODE_FLAG_NHSYNC) != 0);
    CHECK((mode.flags & DRM_MODE_FLAG_NVSYNC) != 0);
    CHECK((mode.flags & DRM_MODE_FLAG_INTERLACE) != 0);

    CHECK_FALSE(wf::layout_detail::parse_modeline(
        "148.50 1920 2008 2052 2200 1080 1084 1089 1125 maybe +vsync", mode));
    CHECK_FALSE(wf::layout_detail::parse_modeline("too short", mode));
}

TEST_CASE("output layout geometry helpers ignore dynamic and disabled outputs")
{
    auto fixed = make_state(wf::OUTPUT_IMAGE_SOURCE_SELF,
        wf::output_config::position_t{100, 200}, make_mode(1920, 1080, 60000), 2.0,
        WL_OUTPUT_TRANSFORM_90);
    auto automatic = make_state(wf::OUTPUT_IMAGE_SOURCE_SELF,
        wf::output_config::position_t{}, make_mode(1280, 720, 60000));
    auto disabled = make_state(wf::OUTPUT_IMAGE_SOURCE_NONE,
        wf::output_config::position_t{0, 0}, make_mode(1280, 720, 60000));

    auto geometry = wf::layout_detail::calculate_output_geometry(fixed);
    CHECK(geometry == wf::geometry_t{100, 200, 540, 960});

    auto config     = config_from_states(std::vector<wf::output_state_t>{fixed, automatic, disabled});
    auto geometries = wf::layout_detail::calculate_fixed_geometries(config);
    REQUIRE(geometries.size() == 1);
    CHECK(geometries[0] == geometry);
    CHECK_FALSE(wf::layout_detail::all_outputs_disabled(config));
    CHECK(wf::layout_detail::all_outputs_disabled(config_from_states(
        std::vector<wf::output_state_t>{disabled})));
}

TEST_CASE("output layout detects overlap touching and disjoint groups")
{
    auto left = make_state(wf::OUTPUT_IMAGE_SOURCE_SELF,
        wf::output_config::position_t{0, 0}, make_mode(100, 100, 60000));
    auto touching = make_state(wf::OUTPUT_IMAGE_SOURCE_SELF,
        wf::output_config::position_t{100, 0}, make_mode(100, 100, 60000));
    auto overlapping = make_state(wf::OUTPUT_IMAGE_SOURCE_SELF,
        wf::output_config::position_t{80, 0}, make_mode(100, 100, 60000));
    auto separate = make_state(wf::OUTPUT_IMAGE_SOURCE_SELF,
        wf::output_config::position_t{250, 0}, make_mode(100, 100, 60000));

    CHECK(wf::layout_detail::are_rectangles_touching({0, 0, 100, 100}, {100, 0, 100, 100}));
    CHECK_FALSE(wf::layout_detail::are_rectangles_touching({0, 0, 100, 100}, {101, 0, 100, 100}));

    CHECK_FALSE(wf::layout_detail::has_overlapping_outputs(config_from_states(std::vector<wf::output_state_t>{
        left, touching})));
    CHECK(wf::layout_detail::has_overlapping_outputs(config_from_states(std::vector<wf::output_state_t>{left,
        overlapping})));

    CHECK_FALSE(wf::layout_detail::has_disjoint_outputs(config_from_states(std::vector<wf::output_state_t>{
        left, touching})));
    CHECK(wf::layout_detail::has_disjoint_outputs(config_from_states(std::vector<wf::output_state_t>{left,
        separate})));
    CHECK_FALSE(wf::layout_detail::has_disjoint_outputs(config_from_states({})));
}
