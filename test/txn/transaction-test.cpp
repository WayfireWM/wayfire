#include "wayfire/signal-provider.hpp"
#include "wayfire/util.hpp"
#include <wayfire/util/log.hpp>
#include <wayfire/debug.hpp>
#include <wayland-server-core.h>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "transaction-test-object.hpp"
#include <wayfire/txn/transaction.hpp>

static void run_transaction_test(bool timeout, bool autoready)
{
    setup_wayfire_debugging_state();
    wf::wl_timer<false>::callback_t tx_timeout_callback;

    wf::txn::transaction_t::timer_setter_t timer_setter =
        [&] (uint64_t time, wf::wl_timer<false>::callback_t cb)
    {
        REQUIRE(time == 1234);
        tx_timeout_callback = cb;
    };

    bool applied = false;
    wf::signal::connection_t<wf::txn::transaction_applied_signal> on_apply =
        [&] (wf::txn::transaction_applied_signal *ev)
    {
        REQUIRE(ev->timed_out == timeout);
        applied = true;
    };

    wf::txn::transaction_t tx(1234, timer_setter);
    tx.connect(&on_apply);
    auto object1 = std::make_shared<txn_test_object_t>(autoready);
    auto object2 = std::make_shared<txn_test_object_t>(autoready);

    tx.add_object(object1);
    tx.add_object(object2);

    // Nothing should happen before commit
    REQUIRE(!tx_timeout_callback);
    REQUIRE(object1->number_applied == 0);
    REQUIRE(object1->number_committed == 0);
    REQUIRE(object2->number_applied == 0);
    REQUIRE(object2->number_committed == 0);
    REQUIRE(tx.get_objects() == std::vector<wf::txn::transaction_object_sptr>{object1, object2});

    tx.commit();
    REQUIRE(tx_timeout_callback);

    if (autoready)
    {
        REQUIRE(object1->number_applied == 1);
        REQUIRE(object1->number_committed == 1);
        REQUIRE(object2->number_applied == 1);
        REQUIRE(object2->number_committed == 1);
        REQUIRE(applied == true);
        return;
    }

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

    if (!timeout)
    {
        object2->emit_ready();
    } else
    {
        tx_timeout_callback();
    }

    REQUIRE(object1->number_applied == 1);
    REQUIRE(object1->number_committed == 1);
    REQUIRE(object2->number_applied == 1);
    REQUIRE(object2->number_committed == 1);
    REQUIRE(applied == true);
}

TEST_CASE("Transaction can be successfully committed and applied")
{
    run_transaction_test(false, false);
}

TEST_CASE("Transaction is applied even after a timeout")
{
    run_transaction_test(true, false);
}

TEST_CASE("Transaction is applied immediately if all objects are ready")
{
    run_transaction_test(false, true);
}

TEST_CASE("Edge cases")
{
    setup_wayfire_debugging_state();
    int applied = 0;
    wf::signal::connection_t<wf::txn::transaction_applied_signal> on_apply =
        [&] (wf::txn::transaction_applied_signal *ev)
    {
        ++applied;
    };

    wf::txn::transaction_t::timer_setter_t timer_setter =
        [&] (uint64_t time, wf::wl_timer<false>::callback_t cb)
    {
        REQUIRE(time == 0);
        cb();
    };

    wf::txn::transaction_t tx(0, timer_setter);
    tx.connect(&on_apply);

    SUBCASE("Empty transaction")
    {}

    SUBCASE("Transaction with timeout=0 without autocommit")
    {
        auto object = std::make_shared<txn_test_object_t>(false);
        tx.add_object(object);
    }

    SUBCASE("Transaction with timeout=0 and autocommit")
    {
        auto object = std::make_shared<txn_test_object_t>(true);
        tx.add_object(object);
    }

    tx.commit();
    REQUIRE(applied == 1);
}
