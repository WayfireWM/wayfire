#include "wayfire/util/duration.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/config/types.hpp>
#include <linux/input-event-codes.h>
#include <limits>

#define WF_CONFIG_DOUBLE_EPS 0.01

using namespace wf;
using namespace wf::option_type;

TEST_CASE("wf::bool_wrapper_t")
{
    CHECK(from_string<bool>("True").value());
    CHECK(from_string<bool>("true").value());
    CHECK(from_string<bool>("TRUE").value());
    CHECK(from_string<bool>("TrUe").value());
    CHECK(from_string<bool>("1").value());

    CHECK(from_string<bool>("False").value() == false);
    CHECK(from_string<bool>("false").value() == false);
    CHECK(from_string<bool>("FALSE").value() == false);
    CHECK(from_string<bool>("FaLSe").value() == false);
    CHECK(from_string<bool>("0").value() == false);

    CHECK(!from_string<bool>("rip"));
    CHECK(!from_string<bool>("1234"));
    CHECK(!from_string<bool>(""));
    CHECK(!from_string<bool>("1h"));
    CHECK(!from_string<bool>("trueeee"));

    CHECK(from_string<bool>(to_string<bool>(true)).value());
    CHECK(from_string<bool>(to_string<bool>(false)).value() == false);
}

TEST_CASE("wf::int_wrapper_t")
{
    CHECK(from_string<int>("456").value() == 456);
    CHECK(from_string<int>("-89").value() == -89);

    int32_t max = std::numeric_limits<int32_t>::max();
    int32_t min = std::numeric_limits<int32_t>::min();
    CHECK(from_string<int>(std::to_string(max)).value() == max);
    CHECK(from_string<int>(std::to_string(min)).value() == min);

    CHECK(!from_string<int>("1e4"));
    CHECK(!from_string<int>(""));
    CHECK(!from_string<int>("1234567890000"));

    CHECK(from_string<int>(to_string<int>(456)).value() == 456);
    CHECK(from_string<int>(to_string<int>(0)).value() == 0);
}

TEST_CASE("wf::double_wrapper_t")
{
    CHECK(from_string<double>("0.378000").value() == doctest::Approx(0.378));
    CHECK(from_string<double>("-89.1847").value() == doctest::Approx(-89.1847));

    double max = std::numeric_limits<double>::max();
    double min = std::numeric_limits<double>::min();
    CHECK(from_string<double>(std::to_string(max)).value() == doctest::Approx(max));
    CHECK(from_string<double>(std::to_string(min)).value() == doctest::Approx(min));

    CHECK(!from_string<double>("1u4"));
    CHECK(!from_string<double>(""));
    CHECK(!from_string<double>("abc"));

    CHECK(from_string<double>(to_string<double>(-4.56)).value() ==
        doctest::Approx(-4.56));
    CHECK(from_string<double>(to_string<double>(0.0)).value() == doctest::Approx(0));
}

static void check_color_equals(const wf::color_t& color,
    double r, double g, double b, double a)
{
    CHECK(color.r == doctest::Approx(r).epsilon(0.01));
    CHECK(color.g == doctest::Approx(g).epsilon(0.01));
    CHECK(color.b == doctest::Approx(b).epsilon(0.01));
    CHECK(color.a == doctest::Approx(a).epsilon(0.01));
}

static void check_color_equals(
    const std::optional<wf::color_t>& color,
    double r, double g, double b, double a)
{
    check_color_equals(color.value(), r, g, b, a);
    CHECK(color == wf::color_t{r, g, b, a});
}

/* Test that various wf::color_t constructors work */
TEST_CASE("wf::color_t")
{
    using namespace wf;
    using namespace option_type;

    check_color_equals(color_t{}, 0, 0, 0, 0);
    check_color_equals(color_t{0.345, 0.127, 0.188, 1.0}, 0.345, 0.127, 0.188, 1.0);
    check_color_equals(color_t{glm::vec4(0.7)}, 0.7, 0.7, 0.7, 0.7);

    check_color_equals(from_string<color_t>("#66CC5ef7"),
        0.4, 0.8, 0.3686274, 0.9686274);
    check_color_equals(from_string<color_t>("#0F0F"), 0, 1, 0, 1);

    check_color_equals(from_string<color_t>("0.34 0.5 0.5 1.0"), 0.34, 0.5, 0.5,
        1.0);

    CHECK(!from_string<color_t>("#FFF"));
    CHECK(!from_string<color_t>("0C1A"));
    CHECK(!from_string<color_t>(""));
    CHECK(!from_string<color_t>("#ZYXUIOPQ"));
    CHECK(!from_string<color_t>("#AUIO")); // invalid color
    CHECK(!from_string<color_t>("1.0 0.5 0.5 1.0 1.0")); // invalid color
    CHECK(!from_string<color_t>("1.0 0.5 0.5 1.0 asdf")); // invalid color
    CHECK(!from_string<color_t>("1.0 0.5")); // invalid color

    CHECK(to_string<color_t>(color_t{0, 0, 0, 0}) == "#00000000");
    CHECK(to_string<color_t>(color_t{0.4, 0.8, 0.3686274,
        0.9686274}) == "#66CC5EF7");
    CHECK(to_string<color_t>(color_t{1, 1, 1, 1}) == "#FFFFFFFF");
}

TEST_CASE("wf::keybinding_t")
{
    /* Test simple constructor */
    const uint32_t modifier1 =
        wf::KEYBOARD_MODIFIER_ALT | wf::KEYBOARD_MODIFIER_LOGO;
    wf::keybinding_t binding1{modifier1, KEY_L};
    CHECK(binding1.get_modifiers() == modifier1);
    CHECK(binding1.get_key() == KEY_L);

    /* Test parsing */
    auto binding2 = from_string<keybinding_t>(
        "<shift><ctrl>KEY_TAB").value();
    uint32_t modifier2 =
        wf::KEYBOARD_MODIFIER_SHIFT | wf::KEYBOARD_MODIFIER_CTRL;
    CHECK(binding2.get_modifiers() == modifier2);
    CHECK(binding2.get_key() == KEY_TAB);

    auto binding3 =
        from_string<keybinding_t>("<alt><super>KEY_L").value();
    CHECK(binding3.get_modifiers() == modifier1);
    CHECK(binding3.get_key() == KEY_L);

    wf::keybinding_t mod_binding = {wf::KEYBOARD_MODIFIER_LOGO, 0};
    CHECK(from_string<keybinding_t>("<super>").value() == mod_binding);

    auto empty = wf::keybinding_t{0, 0};
    CHECK(from_string<keybinding_t>("none").value() == empty);
    CHECK(from_string<keybinding_t>("disabled").value() == empty);

    /* Test invalid bindings */
    CHECK(!from_string<keybinding_t>("<invalid>KEY_L"));
    CHECK(!from_string<keybinding_t>(""));
    CHECK(!from_string<keybinding_t>("<super> KEY_nonexist"));
    CHECK(!from_string<keybinding_t>("<alt> BTN_LEFT"));
    CHECK(!from_string<keybinding_t>("<alt> super KEY_L"));
    CHECK(!from_string<keybinding_t>(("<alt><alt>")));

    /* Test equality */
    CHECK(binding1 == binding3);
    CHECK(!(binding2 == binding1));

    using wk_t = wf::keybinding_t;
    CHECK(from_string<wk_t>(to_string<wk_t>(binding1)).value() == binding1);
    CHECK(from_string<wk_t>(to_string<wk_t>(binding2)).value() == binding2);
}

TEST_CASE("wf::buttonbinding_t")
{
    /* Test simple constructor */
    wf::buttonbinding_t binding1{wf::KEYBOARD_MODIFIER_ALT, BTN_LEFT};
    CHECK(binding1.get_modifiers() == wf::KEYBOARD_MODIFIER_ALT);
    CHECK(binding1.get_button() == BTN_LEFT);

    /* Test parsing */
    auto binding2 =
        from_string<buttonbinding_t>("<ctrl>BTN_EXTRA").value();
    CHECK(binding2.get_modifiers() == wf::KEYBOARD_MODIFIER_CTRL);
    CHECK(binding2.get_button() == BTN_EXTRA);

    auto binding3 =
        from_string<buttonbinding_t>("<alt>BTN_LEFT").value();
    CHECK(binding3.get_modifiers() == wf::KEYBOARD_MODIFIER_ALT);
    CHECK(binding3.get_button() == BTN_LEFT);

    auto empty = wf::buttonbinding_t{0, 0};
    CHECK(from_string<buttonbinding_t>("none").value() == empty);
    CHECK(from_string<buttonbinding_t>("disabled").value() == empty);

    /* Test invalid bindings */
    CHECK(!from_string<buttonbinding_t>("<super> BTN_inv"));
    CHECK(!from_string<buttonbinding_t>("<super> KEY_E"));
    CHECK(!from_string<buttonbinding_t>(""));
    CHECK(!from_string<buttonbinding_t>("<super>"));
    CHECK(!from_string<buttonbinding_t>("super BTN_LEFT"));

    /* Test equality */
    CHECK(binding1 == binding3);
    CHECK(!(binding2 == binding1));

    using wb_t = wf::buttonbinding_t;
    CHECK(from_string<wb_t>(to_string<wb_t>(binding1)).value() == binding1);
    CHECK(from_string<wb_t>(to_string<wb_t>(binding2)).value() == binding2);
    CHECK(from_string<wb_t>(to_string<wb_t>(binding3)).value() == binding3);
}

TEST_CASE("wf::touchgesture_t")
{
    /* Test simple constructor */
    wf::touchgesture_t binding1{wf::GESTURE_TYPE_SWIPE,
        wf::GESTURE_DIRECTION_UP, 3};
    CHECK(binding1.get_type() == wf::GESTURE_TYPE_SWIPE);
    CHECK(binding1.get_finger_count() == 3);
    CHECK(binding1.get_direction() == wf::GESTURE_DIRECTION_UP);

    /* Test parsing */
    auto binding2 =
        from_string<touchgesture_t>("swipe up-left 4").value();
    uint32_t direction2 = wf::GESTURE_DIRECTION_UP | wf::GESTURE_DIRECTION_LEFT;
    CHECK(binding2.get_type() == wf::GESTURE_TYPE_SWIPE);
    CHECK(binding2.get_direction() == direction2);
    CHECK(binding2.get_finger_count() == 4);

    auto binding3 =
        from_string<touchgesture_t>("edge-swipe down 2").value();
    CHECK(binding3.get_type() == wf::GESTURE_TYPE_EDGE_SWIPE);
    CHECK(binding3.get_direction() == wf::GESTURE_DIRECTION_DOWN);
    CHECK(binding3.get_finger_count() == 2);

    auto binding4 =
        from_string<touchgesture_t>("pinch in 3").value();
    CHECK(binding4.get_type() == wf::GESTURE_TYPE_PINCH);
    CHECK(binding4.get_direction() == wf::GESTURE_DIRECTION_IN);
    CHECK(binding4.get_finger_count() == 3);

    auto binding5 =
        from_string<touchgesture_t>("pinch out 2").value();
    CHECK(binding5.get_type() == wf::GESTURE_TYPE_PINCH);
    CHECK(binding5.get_direction() == wf::GESTURE_DIRECTION_OUT);
    CHECK(binding5.get_finger_count() == 2);

    auto empty = wf::touchgesture_t{wf::GESTURE_TYPE_NONE, 0, 0};
    CHECK(from_string<touchgesture_t>("none").value() == empty);
    CHECK(from_string<touchgesture_t>("disabled").value() == empty);

    /* A few bad description cases */
    CHECK(!from_string<touchgesture_t>("pinch out")); // missing fingercount
    CHECK(!from_string<touchgesture_t>("wrong left 5")); // no such type
    CHECK(!from_string<touchgesture_t>("edge-swipe up-down 3")); // opposite dirs
    CHECK(!from_string<touchgesture_t>("swipe 3")); // missing dir
    CHECK(!from_string<touchgesture_t>("pinch 3"));
    CHECK(!from_string<touchgesture_t>(""));

    /* Equality */
    CHECK(!(binding1 == wf::touchgesture_t{wf::GESTURE_TYPE_PINCH, 0, 3}));
    CHECK(binding1 == wf::touchgesture_t{wf::GESTURE_TYPE_SWIPE, 0, 3});
    CHECK(binding3 == wf::touchgesture_t{
        wf::GESTURE_TYPE_EDGE_SWIPE, wf::GESTURE_DIRECTION_DOWN, 2});
    CHECK(binding5 == wf::touchgesture_t{
        wf::GESTURE_TYPE_PINCH, wf::GESTURE_DIRECTION_OUT, 2});
    CHECK(binding5 == wf::touchgesture_t{wf::GESTURE_TYPE_PINCH, 0, 2});
    CHECK(!(binding5 == wf::touchgesture_t{
        wf::GESTURE_TYPE_PINCH, wf::GESTURE_DIRECTION_IN, 2}));

    using wt_t = wf::touchgesture_t;
    CHECK(from_string<wt_t>(to_string<wt_t>(binding1)).value() == binding1);
    CHECK(from_string<wt_t>(to_string<wt_t>(binding2)).value() == binding2);
    CHECK(from_string<wt_t>(to_string<wt_t>(binding3)).value() == binding3);
    CHECK(from_string<wt_t>(to_string<wt_t>(binding4)).value() == binding4);
    CHECK(from_string<wt_t>(to_string<wt_t>(binding5)).value() == binding5);
}

TEST_CASE("wf::hotspot_binding_t")
{
    using hs = wf::hotspot_binding_t;
    const uint32_t tl = OUTPUT_EDGE_TOP | OUTPUT_EDGE_LEFT;

    // sanity check
    auto h1 = hs(tl, 100, 20, 150);
    CHECK(h1.get_edges() == tl);
    CHECK(h1.get_size_along_edge() == 100);
    CHECK(h1.get_size_away_from_edge() == 20);
    CHECK(h1.get_timeout() == 150);

    // parsing
    CHECK(from_string<hs>("hotspot bottom 20x20 1500") ==
        hs(OUTPUT_EDGE_BOTTOM, 20, 20, 1500));
    CHECK(from_string<hs>(to_string(h1)) == h1);

    // check parsing errors
    CHECK(!from_string<hs>("top 10x10 10"));
    CHECK(!from_string<hs>("hotspot topp 10x10 10"));
    CHECK(!from_string<hs>("hotspot top 10x10 10 trailing"));
    CHECK(!from_string<hs>("hotspot top 110 10"));
    CHECK(!from_string<hs>("hotspot top 110"));
}

TEST_CASE("wf::activatorbinding_t")
{
    using namespace wf;
    activatorbinding_t empty_binding{};

    keybinding_t kb1{KEYBOARD_MODIFIER_ALT, KEY_T};
    keybinding_t kb2{KEYBOARD_MODIFIER_LOGO | KEYBOARD_MODIFIER_SHIFT, KEY_TAB};

    buttonbinding_t bb1{KEYBOARD_MODIFIER_CTRL, BTN_EXTRA};
    buttonbinding_t bb2{0, BTN_SIDE};

    touchgesture_t tg1{GESTURE_TYPE_SWIPE, GESTURE_DIRECTION_UP, 3};
    touchgesture_t tg2{GESTURE_TYPE_PINCH, GESTURE_DIRECTION_IN, 4};

    hotspot_binding_t hs{OUTPUT_EDGE_LEFT, 10, 10, 10};

    auto test_binding = [&] (std::string description,
                             bool match_kb1, bool match_kb2, bool match_bb1,
                             bool match_bb2,
                             bool match_tg1, bool match_tg2, bool match_hs)
    {
        auto full_binding_opt = from_string<activatorbinding_t>(description);
        REQUIRE(full_binding_opt);
        auto actbinding = full_binding_opt.value();

        CHECK(actbinding.has_match(kb1) == match_kb1);
        CHECK(actbinding.has_match(kb2) == match_kb2);
        CHECK(actbinding.has_match(bb1) == match_bb1);
        CHECK(actbinding.has_match(bb2) == match_bb2);
        CHECK(actbinding.has_match(tg1) == match_tg1);
        CHECK(actbinding.has_match(tg2) == match_tg2);
        if (match_hs)
        {
            CHECK(actbinding.get_hotspots() == std::vector{hs});
        }
    };

    /** Test all possible combinations */
    for (int k1 = 0; k1 <= 1; k1++)
    {
        for (int k2 = 0; k2 <= 1; k2++)
        {
            for (int b1 = 0; b1 <= 1; b1++)
            {
                for (int b2 = 0; b2 <= 1; b2++)
                {
                    for (auto t1 = 0; t1 <= 1; t1++)
                    {
                        for (auto t2 = 0; t2 <= 1; t2++)
                        {
                            std::string descr;
                            if (k1)
                            {
                                descr += to_string<keybinding_t>(kb1) + " | ";
                            }

                            if (k2)
                            {
                                descr += to_string<keybinding_t>(kb2) + " | ";
                            }

                            if (b1)
                            {
                                descr += to_string<buttonbinding_t>(bb1) + " | ";
                            }

                            if (b2)
                            {
                                descr += to_string<buttonbinding_t>(bb2) + " | ";
                            }

                            if (t1)
                            {
                                descr += to_string<touchgesture_t>(tg1) + " | ";
                            }

                            if (t2)
                            {
                                descr += to_string<touchgesture_t>(tg2) + " | ";
                            }

                            if (descr.length() >= 3)
                            {
                                // remove trailing " | "
                                descr.erase(descr.size() - 3);
                            }

                            test_binding(descr, k1, k2, b1, b2, t1, t2, false);
                            CHECK(to_string<activatorbinding_t>(
                                from_string<activatorbinding_t>(
                                    descr).value()) == descr);
                        }
                    }
                }
            }
        }
    }

    test_binding("none", 0, 0, 0, 0, 0, 0, 0);
    test_binding("disabled | none", 0, 0, 0, 0, 0, 0, 0);
    test_binding("<alt>KEY_T|<alt>KEY_T|none|hotspot left 10x10 10", 1, 0, 0, 0, 0,
        0, 1);

    CHECK(!from_string<activatorbinding_t>("<alt> KEY_K || <alt> KEY_U"));
    CHECK(!from_string<activatorbinding_t>("<alt> KEY_K |"));

    auto with_ext = from_string<activatorbinding_t>("<alt> KEY_T | thrash");
    CHECK(with_ext.has_value() == true);
    CHECK(with_ext->get_extensions().size() == 1);
    CHECK(with_ext->get_extensions().front() == "thrash");
    CHECK(with_ext->has_match(kb1));
}

TEST_CASE("wf::output_config::mode_t")
{
    using mt = wf::output_config::mode_t;
    using namespace wf::output_config;

    std::vector<mt> modes = {
        mt{3},
        mt{2},
        mt{true},
        mt{true},
        mt{false},
        mt{1920, 1080, 0},
        mt{1920, 1080, 59000},
        mt{1920, 1080, 59000},
        mt{std::string{"eDP-1"}},
    };

    std::vector<mode_type_t> types = {
        MODE_HIGHRES,
        MODE_HIGHRR,
        MODE_AUTO,
        MODE_AUTO,
        MODE_OFF,
        MODE_RESOLUTION,
        MODE_RESOLUTION,
        MODE_RESOLUTION,
        MODE_MIRROR,
    };

    std::vector<std::string> desc = {
        "highres",
        "highrr",
        "auto",
        "default",
        "off",
        "1920 x 1080",
        "1920x1080@59",
        "1920x 1080 @ 59000",
        "mirror    eDP-1",
    };

    std::vector<std::string> invalid = {
        "mirroredp",
        "autooo",
        "192e x 1080",
        "1920 1080",
    };

    CHECK(modes[5].get_refresh() == 0);
    CHECK(modes[6].get_refresh() == 59000);
    CHECK(modes[7].get_refresh() == 59000);
    CHECK(modes[8].get_mirror_from() == "eDP-1");

    for (size_t i = 0; i < modes.size(); i++)
    {
        CHECK(modes[i].get_type() == types[i]);
        CHECK(from_string<mt>(desc[i]) == modes[i]);
        CHECK(from_string<mt>(to_string(modes[i])) == modes[i]);

        if (modes[i].get_type() == MODE_RESOLUTION)
        {
            CHECK(modes[i].get_width() == 1920);
            CHECK(modes[i].get_height() == 1080);
        }
    }

    for (auto& d : invalid)
    {
        CHECK(!from_string<mt>(d));
    }
}

TEST_CASE("wf::output_config::position_t")
{
    using pt = wf::output_config::position_t;
    using namespace wf::output_config;

    pt p1 = {0, 0};
    pt p2 = {1920, -1080};

    auto p1c = from_string<pt>("0, 0");
    auto p2c = from_string<pt>("1920 , -1080");

    CHECK(p2c.value().get_x() == 1920);
    CHECK(p2c.value().get_y() == -1080);
    CHECK(!p1c.value().is_automatic_position());
    CHECK(!p2c.value().is_automatic_position());

    CHECK(p1c == p1);
    CHECK(p2c == p2);

    CHECK(from_string<pt>(to_string<pt>(p1)) == p1);
    CHECK(from_string<pt>(to_string<pt>(p2)) == p2);

    CHECK(from_string<pt>("auto").value().is_automatic_position());
    CHECK(from_string<pt>("default").value().is_automatic_position());

    CHECK(!from_string<pt>("test"));
    CHECK(!from_string<pt>("129 129"));
    CHECK(!from_string<pt>("129,"));
}

TEST_CASE("wf::animation::animation_description_t")
{
    using adt = wf::animation_description_t;
    CHECK(!from_string<adt>("test"));
    CHECK(!from_string<adt>("100ss"));
    CHECK(!from_string<adt>("100 ms invalideasing"));
    CHECK(!from_string<adt>("100 ms linear trailing"));

    adt circle100 = {
        .length_ms = 100,
        .easing    = wf::animation::smoothing::circle,
        .easing_name = "circle",
    };
    std::string circle100_str   = "100";
    std::string circle100_str_2 = "100 ms";

    adt linear8s = {
        .length_ms = 8500,
        .easing    = wf::animation::smoothing::linear,
        .easing_name = "linear",
    };
    std::string linear8s_str = "8.5s linear";

    adt sigmoid250ms = {
        .length_ms = 250,
        .easing    = wf::animation::smoothing::sigmoid,
        .easing_name = "sigmoid",
    };
    std::string sigmoid250ms_str = "250ms sigmoid";

    adt custom4s = {
        .length_ms = 4000,
        .easing    = wf::animation::smoothing::get_cubic_bezier(0.25, 0.6, 0.75, 0.4),
        .easing_name = "cubic-bezier 0.25 0.6 0.75 0.4",
    };
    std::string custom4s_str = "4s cubic-bezier 0.25 0.6 0.75 0.4";

    adt custom333ms = {
        .length_ms = 333,
        .easing    = wf::animation::smoothing::get_cubic_bezier(0.16, 1, 0.3, 1),
        .easing_name = "cubic-bezier 0.1600 1.0000 0.3000 1.0000",
    };
    std::string custom333ms_str = "333ms cubic-bezier 0.16 1 0.3 1";

    CHECK(from_string<adt>(circle100_str) == circle100);
    CHECK(from_string<adt>(circle100_str_2) == circle100);
    CHECK(from_string<adt>(linear8s_str) == linear8s);
    CHECK(from_string<adt>(sigmoid250ms_str) == sigmoid250ms);
    CHECK(from_string<adt>(custom4s_str) == custom4s);
    CHECK(from_string<adt>(custom333ms_str) == custom333ms);
    CHECK(from_string<adt>(to_string<adt>(custom333ms)) == from_string<adt>(custom333ms_str));
    CHECK(to_string<adt>(sigmoid250ms) == sigmoid250ms_str);
}
