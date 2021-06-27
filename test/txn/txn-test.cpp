#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../src/core/transaction/transaction-priv.hpp"
#include "mock-instruction.hpp"
#include "../mock-core.hpp"
#include "../mock.hpp"

using namespace wf::txn;

TEST_CASE("Transaction Impl Basics")
{
    // Register a fake view a
    mock_core().fake_views["a"] =
        wayfire_view((wf::view_interface_t*)0x1234);

    auto tx_pub = transaction_t::create();
    auto tx_ab  = dynamic_cast<transaction_impl_t*>(tx_pub.get());
    REQUIRE(tx_ab != nullptr);

    auto i1 = new mock_instruction_t("a");
    auto i2 = new mock_instruction_t("b");
    auto i3 = new mock_instruction_t("b");

    auto i4 = new mock_instruction_t("a");
    auto i5 = new mock_instruction_t("c");

    tx_ab->add_instruction(instruction_uptr_t(i1));
    tx_ab->add_instruction(instruction_uptr_t(i2));
    tx_ab->add_instruction(instruction_uptr_t(i3));

    REQUIRE(tx_ab->get_objects() == std::set<std::string>{"a", "b"});
    REQUIRE(tx_ab->get_views() ==
        std::set<wayfire_view>{mock_core().fake_views["a"]});

    auto tx_c = transaction_t::create();
    tx_c->add_instruction(instruction_uptr_t(i5));

    auto tx_a = transaction_t::create();
    tx_a->add_instruction(instruction_uptr_t(i4));

    REQUIRE(tx_ab->does_intersect(dynamic_cast<transaction_impl_t&>(*tx_a)));
    REQUIRE_FALSE(tx_ab->does_intersect(dynamic_cast<transaction_impl_t&>(*tx_c)));

    auto tx_a_priv = dynamic_cast<transaction_impl_t*>(tx_a.release());
    auto tx_c_priv = dynamic_cast<transaction_impl_t*>(tx_c.release());

    tx_ab->merge(transaction_iuptr_t(tx_a_priv));
    REQUIRE(tx_ab->get_objects() == std::set<std::string>{"a", "b"});
    REQUIRE(tx_ab->get_views() ==
        std::set<wayfire_view>{mock_core().fake_views["a"]});

    tx_ab->merge(transaction_iuptr_t(tx_c_priv));
    REQUIRE(tx_ab->get_objects() == std::set<std::string>{"a", "b", "c"});
    REQUIRE(tx_ab->get_views() ==
        std::set<wayfire_view>{mock_core().fake_views["a"]});

    const auto& check_instructions = [&] (int nr_pending,
                                          int nr_committed, int nr_applied)
    {
        for (auto i : {i1, i2, i3, i4, i5})
        {
            REQUIRE(i->pending == nr_pending);
            REQUIRE(i->committed == nr_committed);
            REQUIRE(i->applied == nr_applied);
        }
    };

    check_instructions(0, 0, 0);
    tx_ab->set_id(123);
    REQUIRE(tx_ab->get_state() == TXN_NEW);
    REQUIRE(tx_ab->get_id() == 123);

    tx_ab->set_pending();
    check_instructions(1, 0, 0);
    REQUIRE(tx_ab->get_state() == TXN_PENDING);

    tx_ab->commit();
    check_instructions(1, 1, 0);
    REQUIRE(tx_ab->get_state() == TXN_COMMITTED);

    for (auto i : {i1, i2, i3, i4, i5})
    {
        i->send_ready();
    }

    REQUIRE(tx_ab->get_state() == TXN_READY);

    tx_ab->apply();
    check_instructions(1, 1, 1);
    REQUIRE(tx_ab->get_state() == TXN_APPLIED);
}

TEST_CASE("Transaction Impl Signals")
{
    mock_loop::get().start(0);

    auto tx_pub = transaction_t::create();
    auto tx_ab  = dynamic_cast<transaction_impl_t*>(tx_pub.get());
    REQUIRE(tx_ab != nullptr);

    auto i1 = new mock_instruction_t("a");
    auto i2 = new mock_instruction_t("b");

    tx_ab->add_instruction(instruction_uptr_t(i1));

    int nr_ready     = 0;
    int nr_cancelled = 0;
    int nr_timeout   = 0;

    const auto& check_states = [&] (int applied, int cancelled, int timeout)
    {
        REQUIRE(nr_ready == applied);
        REQUIRE(nr_cancelled == cancelled);
        REQUIRE(nr_timeout == timeout);
    };

    wf::signal_callback_t on_done = [&] (wf::signal_data_t *data)
    {
        auto ev = static_cast<priv_done_signal*>(data);
        REQUIRE(ev->id == 0);

        switch (ev->state)
        {
          case wf::txn::TXN_TIMED_OUT:
            ++nr_timeout;
            break;

          case wf::txn::TXN_CANCELLED:
            ++nr_cancelled;
            break;

          case wf::txn::TXN_READY:
            ++nr_ready;
            break;

          default:
            REQUIRE_MESSAGE(false, "done signal emitted with invalid end state!");
        }
    };

    tx_ab->connect_signal("done", &on_done);
    tx_ab->set_id(0);

    SUBCASE("Merge into pending, then cancel")
    {
        tx_ab->set_pending();
        auto tx_pub_b = transaction_t::create();
        auto tx_b     = dynamic_cast<transaction_impl_t*>(tx_pub_b.release());

        tx_b->add_instruction(instruction_uptr_t(i2));
        tx_ab->merge(transaction_iuptr_t(tx_b));

        REQUIRE(tx_ab->get_objects() == std::set<std::string>{"a", "b"});
        tx_ab->commit();
        i2->send_cancel();
        check_states(0, 1, 0);
    }

    SUBCASE("No merging")
    {
        tx_ab->add_instruction(instruction_uptr_t(i2));
        tx_ab->set_pending();
        tx_ab->commit();

        SUBCASE("Cancelling")
        {
            i1->send_ready();
            i2->send_cancel();
            check_states(0, 1, 0);
            mock_loop::get().move_forward(1000);
            check_states(0, 1, 0);

            REQUIRE(tx_ab->get_state() == TXN_CANCELLED);
        }

        SUBCASE("Time out")
        {
            i2->send_ready();
            // Move a lot forward so that it has time to trigger the timeout
            mock_loop::get().move_forward(1000);
            check_states(0, 0, 1);

            REQUIRE(tx_ab->get_state() == TXN_TIMED_OUT);
        }

        SUBCASE("Successful apply")
        {
            i2->send_ready();
            // Default timeout is 100ms
            // Make sure that it is almost triggered
            mock_loop::get().move_forward(99);
            i1->send_ready();
            check_states(1, 0, 0);
            mock_loop::get().move_forward(1000);
            check_states(1, 0, 0);
            REQUIRE(tx_ab->get_state() == TXN_READY);
        }
    }
}

TEST_CASE("Transaction impl dirty flag")
{
    auto tx_pub = transaction_t::create();
    auto tx_ab  = dynamic_cast<transaction_impl_t*>(tx_pub.get());
    REQUIRE(tx_ab != nullptr);

    auto i1 = new mock_instruction_t("a");
    auto i2 = new mock_instruction_t("b");

    tx_ab->add_instruction(instruction_uptr_t(i1));
    REQUIRE(tx_ab->is_dirty());
    tx_ab->set_pending();
    REQUIRE(tx_ab->is_dirty());
    tx_ab->clear_dirty();
    REQUIRE_FALSE(tx_ab->is_dirty());

    tx_ab->add_instruction(instruction_uptr_t(i2));
    REQUIRE(tx_ab->is_dirty());
}
