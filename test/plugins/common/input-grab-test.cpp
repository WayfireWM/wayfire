#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/core.hpp>
#include <wayfire/plugins/common/input-grab.hpp>

#include "../../support/headless-core-harness.hpp"
#include "../../../src/core/core-impl.hpp"
#include "../../../src/core/seat/cursor.hpp"
#include "../../../src/core/seat/seat-impl.hpp"

namespace
{
class test_pointer_interaction_t : public wf::pointer_interaction_t
{
  public:
    int enter_count = 0;
    wf::pointf_t last_enter = {0, 0};
    wf::input_grab_kind_t last_grab = wf::input_grab_kind_t::NONE;

    void handle_pointer_enter(wf::pointf_t position,
        wf::input_grab_kind_t grab) override
    {
        enter_count++;
        last_enter = position;
        last_grab  = grab;
    }
};
}

TEST_CASE("input grab resets cursor on pointer enter")
{
    wf::test::headless_core_harness_t harness;
    test_pointer_interaction_t pointer;
    wf::scene::grab_node_t grab_node{"test", harness.output(), nullptr, &pointer, nullptr};

    wf::get_core().set_cursor("crosshair");
    REQUIRE(wf::get_core_impl().seat->priv->cursor->last_cursor_name == "crosshair");

    grab_node.pointer_interaction().handle_pointer_enter({10, 20}, wf::input_grab_kind_t::EXPLICIT);

    CHECK(wf::get_core_impl().seat->priv->cursor->last_cursor_name == "left_ptr");
    CHECK(pointer.enter_count == 1);
    CHECK(pointer.last_enter == wf::pointf_t{10, 20});
    CHECK(pointer.last_grab == wf::input_grab_kind_t::EXPLICIT);
}
