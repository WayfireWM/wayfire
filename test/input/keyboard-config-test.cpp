#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/core.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <xkbcommon/xkbcommon.h>

#include "../support/headless-core-harness.hpp"

namespace
{
static void led_update(wlr_keyboard*, uint32_t)
{}

static const wlr_keyboard_impl test_keyboard_impl = {
    .name = "wayfire-test-keyboard",
    .led_update = led_update,
};

class temporary_file_t
{
  public:
    temporary_file_t()
    {
        char tmpl[] = "/tmp/wayfire-xkb-keymap-XXXXXX";
        fd = mkstemp(tmpl);
        REQUIRE(fd >= 0);
        path = tmpl;
    }

    temporary_file_t(const temporary_file_t&) = delete;
    temporary_file_t& operator =(const temporary_file_t&) = delete;

    temporary_file_t(temporary_file_t&& other) noexcept
    {
        fd   = other.fd;
        path = std::move(other.path);
        other.fd = -1;
    }

    temporary_file_t& operator =(temporary_file_t&& other) noexcept = delete;

    ~temporary_file_t()
    {
        if (fd >= 0)
        {
            close(fd);
        }

        if (!path.empty())
        {
            std::remove(path.c_str());
        }
    }

    void write(const std::string& contents)
    {
        auto file = fdopen(fd, "w");
        REQUIRE(file);
        fd = -1;
        REQUIRE(fputs(contents.c_str(), file) >= 0);
        REQUIRE(fclose(file) == 0);
    }

    std::string path;

  private:
    int fd = -1;
};

class test_keyboard_t
{
  public:
    test_keyboard_t()
    {
        wlr_keyboard_init(&keyboard, &test_keyboard_impl, "wayfire-test-keyboard");
        wl_signal_emit_mutable(&wf::get_core().backend->events.new_input, &keyboard.base);
    }

    ~test_keyboard_t()
    {
        wlr_keyboard_finish(&keyboard);
    }

    wlr_keyboard keyboard = {};
};

xkb_keymap *create_named_keymap(xkb_context *ctx, const char *layout)
{
    xkb_rule_names names = {};
    names.layout = layout;
    return xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

std::string get_first_layout_name(xkb_keymap *keymap)
{
    REQUIRE(keymap);
    REQUIRE(xkb_keymap_num_layouts(keymap) >= 1);
    auto layout = xkb_keymap_layout_get_name(keymap, 0);
    REQUIRE(layout);
    return layout;
}

temporary_file_t create_keymap_file(const char *layout, std::string& first_layout)
{
    auto ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    REQUIRE(ctx);

    auto keymap = create_named_keymap(ctx, layout);
    REQUIRE(keymap);
    first_layout = get_first_layout_name(keymap);

    auto serialized = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    REQUIRE(serialized);

    temporary_file_t file;
    file.write(serialized);

    free(serialized);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
    return file;
}

std::string get_named_first_layout(const char *layout)
{
    auto ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    REQUIRE(ctx);

    auto keymap = create_named_keymap(ctx, layout);
    REQUIRE(keymap);
    auto first_layout = get_first_layout_name(keymap);

    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
    return first_layout;
}
}

TEST_CASE("keyboard loads xkb_file through compositor input config")
{
    std::string expected_layout;
    auto file   = create_keymap_file("de", expected_layout);
    auto config = std::string{"[input]\n"} +
    "xkb_file = " + file.path + "\n" +
    "xkb_layout = us\n";

    wf::test::headless_core_harness_t harness{config};
    test_keyboard_t keyboard;

    REQUIRE(keyboard.keyboard.keymap);
    CHECK(get_first_layout_name(keyboard.keyboard.keymap) == expected_layout);
}

TEST_CASE("keyboard falls back to xkb_layout when xkb_file cannot be loaded")
{
    auto expected_layout = get_named_first_layout("us");

    wf::test::headless_core_harness_t harness{
        "[input]\n"
        "xkb_file = /tmp/wayfire-missing-xkb-keymap\n"
        "xkb_layout = us\n"};
    test_keyboard_t keyboard;

    REQUIRE(keyboard.keyboard.keymap);
    CHECK(get_first_layout_name(keyboard.keyboard.keymap) == expected_layout);
}
