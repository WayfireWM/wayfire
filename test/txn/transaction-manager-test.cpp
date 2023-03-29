#include "wayfire/signal-provider.hpp"
#include "wayfire/txn/transaction-manager.hpp"
#include "wayfire/util.hpp"
#include <wayfire/util/log.hpp>
#include <wayfire/debug.hpp>
#include <wayland-server-core.h>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "transaction-test-object.hpp"
#include <wayfire/txn/transaction.hpp>
#include "../../src/core/txn/transaction-manager-impl.hpp"

static wf::txn::transaction_uptr new_tx()
{
    return std::make_unique<wf::txn::transaction_t>(0, [] (auto, auto) {});
}

TEST_CASE("Simple transaction is scheduled and executed")
{
    setup_wayfire_debugging_state();
    wf::txn::transaction_manager_t::impl mgr;

    auto obj = std::make_shared<txn_test_object_t>(false);
    auto tx  = new_tx();
    tx->add_object(obj);

    mgr.schedule_transaction(std::move(tx));
    REQUIRE(mgr.committed.size() == 1);
    REQUIRE(mgr.pending.size() == 0);
    REQUIRE(obj->number_committed == 1);
    REQUIRE(obj->number_applied == 0);

    obj->emit_ready();
    REQUIRE(obj->number_committed == 1);
    REQUIRE(obj->number_applied == 1);
    REQUIRE(mgr.committed.size() == 0);
    REQUIRE(mgr.pending.size() == 0);
    REQUIRE(mgr.done.size() == 1);

    wl_event_loop_dispatch_idle(wf::wl_idle_call::loop);
    REQUIRE(mgr.committed.size() == 0);
    REQUIRE(mgr.pending.size() == 0);
    REQUIRE(mgr.done.size() == 0);
}

TEST_CASE("Transactions for the same object wait on each other")
{
    setup_wayfire_debugging_state();
    wf::txn::transaction_manager_t::impl mgr;

    auto obj = std::make_shared<txn_test_object_t>(false);
    auto tx1 = new_tx();
    tx1->add_object(obj);
    mgr.schedule_transaction(std::move(tx1));
    wl_event_loop_dispatch_idle(wf::wl_idle_call::loop);

    REQUIRE(mgr.committed.size() == 1);
    REQUIRE(mgr.pending.size() == 0);

    auto tx2 = new_tx();
    tx2->add_object(obj);
    mgr.schedule_transaction(std::move(tx2));
    wl_event_loop_dispatch_idle(wf::wl_idle_call::loop);

    REQUIRE(mgr.committed.size() == 1);
    REQUIRE(mgr.pending.size() == 1);

    // tx1 is now ready, tx2 is committed
    obj->emit_ready();
    REQUIRE(mgr.done.size() == 1);
    REQUIRE(mgr.committed.size() == 1);
    REQUIRE(mgr.pending.size() == 0);
    REQUIRE(obj->number_committed == 2);
    REQUIRE(obj->number_applied == 1);

    wl_event_loop_dispatch_idle(wf::wl_idle_call::loop);
    REQUIRE(mgr.done.size() == 0);
    REQUIRE(mgr.committed.size() == 1);
    REQUIRE(mgr.pending.size() == 0);
    REQUIRE(obj->number_committed == 2);
    REQUIRE(obj->number_applied == 1);

    obj->emit_ready();
    REQUIRE(mgr.committed.size() == 0);
    REQUIRE(mgr.done.size() == 1);
    REQUIRE(mgr.pending.size() == 0);
    REQUIRE(obj->number_applied == 2);
}

TEST_CASE("Transactions are merged correctly")
{
    setup_wayfire_debugging_state();
    wf::txn::transaction_manager_t::impl mgr;

    auto obj_a = std::make_shared<txn_test_object_t>(false);
    auto obj_b = std::make_shared<txn_test_object_t>(false);
    auto obj_c = std::make_shared<txn_test_object_t>(false);

    auto tx0 = new_tx();
    auto tx1 = new_tx();
    auto tx2 = new_tx();
    auto tx3 = new_tx();
    auto tx4 = new_tx();

    // Block the other transactions from happening
    tx0->add_object(obj_a);
    tx0->add_object(obj_b);
    tx0->add_object(obj_c);
    mgr.schedule_transaction(std::move(tx0));

    tx1->add_object(obj_a);
    tx1->add_object(obj_b);

    tx2->add_object(obj_a);

    tx3->add_object(obj_b);
    tx3->add_object(obj_c);

    tx4->add_object(obj_a);
    tx4->add_object(obj_b);

    // tx1 is scheduled and committed immediately
    mgr.schedule_transaction(std::move(tx1));

    // tx2, tx3, and tx4 should be merged together
    mgr.schedule_transaction(std::move(tx2));
    mgr.schedule_transaction(std::move(tx3));
    mgr.schedule_transaction(std::move(tx4));

    REQUIRE(mgr.pending.size() == 1);
    REQUIRE(mgr.committed.size() == 1);
    REQUIRE(mgr.done.size() == 0);
    REQUIRE(mgr.pending.front()->get_objects().size() == 3);
}

TEST_CASE("Transactions which are immediately ready also work")
{
    setup_wayfire_debugging_state();
    wf::txn::transaction_manager_t::impl mgr;

    auto obj = std::make_shared<txn_test_object_t>(true);
    auto tx1 = new_tx();
    tx1->add_object(obj);
    mgr.schedule_transaction(std::move(tx1));
    wl_event_loop_dispatch_idle(wf::wl_idle_call::loop);

    // dispatch_idle will execute all idle callbacks: the first callback commits, then the transaction is
    // immediately applied, and after that, it is cleaned up on a second idle run.
    REQUIRE(mgr.done.size() == 0);
    REQUIRE(mgr.committed.size() == 0);
    REQUIRE(mgr.pending.size() == 0);
    REQUIRE(obj->number_applied == 1);
}

TEST_CASE("Non-conflicting transactions are scheduled together")
{
    setup_wayfire_debugging_state();
    wf::txn::transaction_manager_t::impl mgr;

    auto obj_a = std::make_shared<txn_test_object_t>(false);
    auto tx1   = new_tx();
    tx1->add_object(obj_a);

    auto obj_b = std::make_shared<txn_test_object_t>(false);
    auto tx2   = new_tx();
    tx2->add_object(obj_b);

    mgr.schedule_transaction(std::move(tx1));
    mgr.schedule_transaction(std::move(tx2));
    wl_event_loop_dispatch_idle(wf::wl_idle_call::loop);
    REQUIRE(mgr.done.size() == 0);
    REQUIRE(mgr.committed.size() == 2);
    REQUIRE(mgr.pending.size() == 0);

    REQUIRE(obj_a->number_committed == 1);
    REQUIRE(obj_b->number_committed == 1);
}

TEST_CASE("Schedule from apply()")
{
    setup_wayfire_debugging_state();
    wf::txn::transaction_manager_t::impl mgr;

    auto obj_a = std::make_shared<txn_test_object_t>(true);
    auto obj_b = std::make_shared<txn_test_object_t>(true);
    auto tx1   = new_tx();
    tx1->add_object(obj_a);

    auto tx2 = new_tx();
    tx2->add_object(obj_b);

    obj_a->apply_callback = [&]
    {
        REQUIRE(obj_a->number_applied == 1);
        REQUIRE(obj_a->number_committed == 1);
        mgr.schedule_transaction(std::move(tx2));
    };

    mgr.schedule_transaction(std::move(tx1));
    REQUIRE(mgr.committed.size() == 0);
    REQUIRE(mgr.pending.size() == 0);
    REQUIRE(mgr.done.size() == 2);
    REQUIRE(obj_b->number_committed == 1);
    REQUIRE(obj_b->number_applied == 1);
}

TEST_CASE("Schedule from apply() with blocking")
{
    setup_wayfire_debugging_state();
    wf::txn::transaction_manager_t::impl mgr;

    auto obj_a = std::make_shared<txn_test_object_t>(false);
    auto obj_b = std::make_shared<txn_test_object_t>(true);

    auto tx1 = new_tx();
    tx1->add_object(obj_a);

    auto tx2 = new_tx();
    tx2->add_object(obj_b);

    bool added = false;
    obj_b->apply_callback = [&]
    {
        if (added)
        {
            return;
        }

        // Avoid infinite recursion, otherwise the second object will continuously be committed.
        added = true;

        auto tx1_2 = new_tx();
        tx1_2->add_object(obj_a);
        auto tx2_2 = new_tx();
        tx2_2->add_object(obj_b);

        REQUIRE(obj_a->number_applied == 0);
        REQUIRE(obj_a->number_committed == 1);
        mgr.schedule_transaction(std::move(tx1_2));
        mgr.schedule_transaction(std::move(tx2_2));
    };

    mgr.schedule_transaction(std::move(tx1));
    mgr.schedule_transaction(std::move(tx2));

    REQUIRE(mgr.committed.size() == 1);
    REQUIRE(mgr.pending.size() == 1);
    REQUIRE(mgr.done.size() == 2);
    REQUIRE(obj_b->number_committed == 2);
    REQUIRE(obj_b->number_applied == 2);
    REQUIRE(obj_a->number_committed == 1);
    REQUIRE(obj_a->number_applied == 0);
}

TEST_CASE("Concurrent committed")
{
    // This testcase exists to check that the code can handle multiple committed transactions and that
    // committed transactions are properly moved to the done array.
    setup_wayfire_debugging_state();
    wf::txn::transaction_manager_t::impl mgr;

    auto obj_a = std::make_shared<txn_test_object_t>(false);
    auto obj_b = std::make_shared<txn_test_object_t>(false);

    auto tx1 = new_tx();
    tx1->add_object(obj_a);

    auto tx2 = new_tx();
    tx2->add_object(obj_a);

    auto tx3 = new_tx();
    tx3->add_object(obj_b);

    // Make sure tx3 is before tx2
    mgr.schedule_transaction(std::move(tx1));
    mgr.schedule_transaction(std::move(tx3));
    mgr.schedule_transaction(std::move(tx2));
    REQUIRE(mgr.committed.size() == 2);
    REQUIRE(mgr.pending.size() == 1);
    REQUIRE(mgr.done.size() == 0);

    obj_a->emit_ready();
    REQUIRE(mgr.committed.size() == 2);
    REQUIRE(mgr.pending.size() == 0);
    REQUIRE(mgr.done.size() == 1);

    obj_a->emit_ready();
    REQUIRE(mgr.committed.size() == 1);
    REQUIRE(mgr.pending.size() == 0);
    REQUIRE(mgr.done.size() == 2);
}
