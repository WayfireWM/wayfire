#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/bindings-repository.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>

#include <linux/input-event-codes.h>

#include "../../support/headless-core-harness.hpp"
#include "../../support/scoped-env.hpp"

TEST_CASE("vswitch last binding follows external workspace changes")
{
    wf::test::scoped_env_t plugin_path{"WAYFIRE_PLUGIN_PATH", TEST_PLUGIN_PATH};

    wf::test::headless_core_harness_t harness{
        "[core]\n"
        "plugins = vswitch\n"
        "vwidth = 2\n"
        "vheight = 1\n"
        "\n"
        "[vswitch]\n"
        "binding_last = <ctrl> KEY_F12\n"
        "duration = 0\n",
        true};

    auto *output = harness.output();
    REQUIRE(output);

    auto wset = output->wset();
    REQUIRE(wset);
    REQUIRE(wset->get_current_workspace() == wf::point_t{0, 0});

    wset->set_workspace({1, 0});
    REQUIRE(wset->get_current_workspace() == wf::point_t{1, 0});

    wf::keybinding_t last_binding{WLR_MODIFIER_CTRL, KEY_F12};
    REQUIRE(wf::get_core().bindings->handle_key(last_binding, 0));
    CHECK(wset->get_current_workspace() == wf::point_t{0, 0});
}
