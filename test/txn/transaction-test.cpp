#include "wayfire/signal-provider.hpp"
#include "wayfire/util.hpp"
#include <wayfire/util/log.hpp>
#include <wayfire/debug.hpp>
#include <wayland-server-core.h>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "transaction-test-object.hpp"
#include <wayfire/txn/transaction.hpp>

TEST_CASE("Transaction can be successfully committed and applied")
{
    setup_wayfire_debugging_state();
    wf::wl_timer::callback_t tx_timeout_callback;

    wf::txn::transaction_t::timer_setter_t timer_setter = [&] (uint64_t time, wf::wl_timer::callback_t cb)
    {
        REQUIRE(time == 1234);
        tx_timeout_callback = cb;
    };

    bool applied = false;
    wf::signal::connection_t<wf::txn::transaction_applied_signal> on_apply = [&] (auto)
    {
        applied = true;
    };

    wf::txn::transaction_t tx(1234);
    tx.connect(&on_apply);
    auto object1 = std::make_shared<txn_test_object_t>(false);
    auto object2 = std::make_shared<txn_test_object_t>(false);

    tx.add_object(object1);
    tx.add_object(object2);

    // Nothing should happen before commit
    REQUIRE(!tx_timeout_callback);
    REQUIRE(object1->number_applied == 0);
    REQUIRE(object1->number_committed == 0);
    REQUIRE(object2->number_applied == 0);
    REQUIRE(object2->number_committed == 0);
    REQUIRE(tx.get_objects() == std::vector<wf::txn::transaction_object_sptr>{object1, object2});

    tx.commit(timer_setter);
    REQUIRE(tx_timeout_callback);
    REQUIRE(object1->number_applied == 0);
    REQUIRE(object1->number_committed == 1);
    REQUIRE(object2->number_applied == 0);
    REQUIRE(object2->number_committed == 1);

    object1->emit_ready();
    REQUIRE(object1->number_applied == 0);
    REQUIRE(object1->number_committed == 1);
    REQUIRE(object2->number_applied == 0);
    REQUIRE(object2->number_committed == 1);
    REQUIRE(applied == false);

    object2->emit_ready();
    REQUIRE(object1->number_applied == 1);
    REQUIRE(object1->number_committed == 1);
    REQUIRE(object2->number_applied == 1);
    REQUIRE(object2->number_committed == 1);
    REQUIRE(applied == true);
}
