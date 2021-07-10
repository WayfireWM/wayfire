#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/compositor-view.hpp>
#include "../src/core/transaction/transaction-priv.hpp"
#include "mock-instruction.hpp"
#include "../mock-core.hpp"
#include "../mock.hpp"

using namespace wf::txn;
TEST_CASE("Getting IDs")
{
    auto tx     = transaction_t::create();
    auto tx_raw = tx.get();
    tx->add_instruction(mock_instruction_t::get("a"));

    auto tx2     = transaction_t::create();
    auto tx2_raw = tx2.get();
    tx2->add_instruction(mock_instruction_t::get("a"));

    auto& manager = get_fresh_transaction_manager();

    auto id  = manager.submit(std::move(tx));
    auto id2 = manager.submit(std::move(tx2));

    REQUIRE(id == tx_raw->get_id());
    REQUIRE(id2 == tx2_raw->get_id());
    REQUIRE(id != id2);
}

TEST_CASE("Submit and extend transaction")
{
    auto& manager = get_fresh_transaction_manager();
    auto& core    = mock_core();

    wf::color_rect_view_t fake_view;
    core.fake_views["b"] = {&fake_view};

    auto tx = transaction_t::create();
    tx->add_instruction(mock_instruction_t::get("a"));

    int nr_pending = 0;
    int nr_view_pending = 0;

    // Add A b and C consecutively
    std::vector<std::string> in_pending = {"A", "b", "C"};
    // Add only X on the view signal
    std::vector<std::string> in_view_pending = {"X"};

    wf::signal_connection_t on_pending = [&] (wf::signal_data_t *data)
    {
        auto ev = static_cast<pending_signal*>(data);
        if (nr_pending < (int)in_pending.size())
        {
            ev->tx->add_instruction(
                mock_instruction_t::get(in_pending[nr_pending]));
        }

        ++nr_pending;
    };

    wf::signal_connection_t on_view_pending = [&] (wf::signal_data_t *data)
    {
        auto ev = static_cast<pending_signal*>(data);
        if (nr_view_pending < (int)in_view_pending.size())
        {
            ev->tx->add_instruction(
                mock_instruction_t::get(in_view_pending[nr_view_pending]));
        }

        ++nr_view_pending;
    };

    manager.connect_signal("pending", &on_pending);
    fake_view.connect_signal("transaction-pending", &on_view_pending);

    auto tx_raw = tx.get();
    manager.submit(std::move(tx));

    REQUIRE(nr_pending == 4);
    REQUIRE(nr_view_pending == 3);

    REQUIRE(tx_raw->get_objects() ==
        std::set<std::string>{"a", "A", "b", "C", "X"});

    // Clear up for other tests
    core.fake_views.clear();
}

TEST_CASE("Commit and then apply transaction")
{
    auto& manager = get_fresh_transaction_manager();
    auto tx = transaction_t::create();

    int nr_instruction_freed = 0;
    auto i = new mock_instruction_t("a");
    i->cnt_destroy = &nr_instruction_freed;
    tx->add_instruction(instruction_uptr_t(i));

    std::map<int, int> nr_pending;
    std::map<int, int> nr_ready;
    std::map<int, int> nr_done;

    wf::signal_connection_t on_pending = [&] (wf::signal_data_t *data)
    {
        auto ev = static_cast<pending_signal*>(data);
        REQUIRE(i->pending == 1);
        REQUIRE(nr_ready[ev->tx->get_id()] == 0);
        REQUIRE(nr_done[ev->tx->get_id()] == 0);
        nr_pending[ev->tx->get_id()]++;
    };
    wf::signal_connection_t on_ready = [&] (wf::signal_data_t *data)
    {
        auto ev = static_cast<ready_signal*>(data);
        REQUIRE(nr_done[ev->tx->get_id()] == 0);
        nr_ready[ev->tx->get_id()]++;
    };
    wf::signal_connection_t on_done = [&] (wf::signal_data_t *data)
    {
        auto ev = static_cast<done_signal*>(data);
        nr_done[ev->tx->get_id()]++;
    };

    manager.connect_signal("pending", &on_pending);
    manager.connect_signal("ready", &on_ready);
    manager.connect_signal("done", &on_done);

    // 0-> new, 1 -> pending, 2-> committed, 3->ready, 4-> done
    const auto& require = [&nr_pending, &nr_ready, &nr_done] (
        int id, mock_instruction_t *i, int phase)
    {
        REQUIRE(nr_pending[id] == (phase >= 1));
        REQUIRE(nr_ready[id] == (phase >= 3));
        REQUIRE(nr_done[id] == (phase >= 4));

        REQUIRE(i->pending == (phase >= 1));
        REQUIRE(i->committed == (phase >= 2));
        REQUIRE(i->applied == (phase >= 4));
    };

    auto id1 = manager.submit(std::move(tx));
    SUBCASE("Schedule two concurrent transactions")
    {
        auto tx2 = transaction_t::create();
        auto i2  = new mock_instruction_t("b");
        i2->cnt_destroy = &nr_instruction_freed;
        tx2->add_instruction(instruction_uptr_t(i2));
        auto id2 = manager.submit(std::move(tx2));

        require(id1, i, 1);
        require(id2, i2, 1);

        // really waits for ready
        for (int cnt = 0; cnt < 5; cnt++)
        {
            mock_loop::get().dispatch_idle();
            require(id1, i, 2);
            require(id2, i, 2);
        }

        i->send_ready();
        require(id1, i, 4);
        require(id2, i2, 2);
        mock_loop::get().dispatch_idle();
        REQUIRE(nr_instruction_freed == 1);

        i2->send_ready();
        require(id2, i2, 4);
        mock_loop::get().dispatch_idle();
        REQUIRE(nr_instruction_freed == 2);
    }

    SUBCASE("Transaction times out")
    {
        mock_loop::get().dispatch_idle();
        require(id1, i, 2);
        mock_loop::get().move_forward(100);
        require(id1, i, 4);
    }


    const auto& require_cancel = [&] (int id, mock_instruction_t *i)
    {
        REQUIRE(nr_done[id] == 1);
        REQUIRE(nr_ready[id] == 0);
        REQUIRE(i->applied == 0);
    };

    SUBCASE("Pending and committed transactions are cancelled, another is scheduled")
    {
        auto tx2 = transaction_t::create();
        auto i2  = new mock_instruction_t("a");
        tx2->add_instruction(instruction_uptr_t(i2));

        auto tx3 = transaction_t::create();
        auto i3  = new mock_instruction_t("b");
        tx3->add_instruction(instruction_uptr_t(i3));

        mock_loop::get().dispatch_idle();
        require(id1, i, 2);

        auto id2 = manager.submit(std::move(tx2));
        auto id3 = manager.submit(std::move(tx3));

        require(id2, i2, 1);
        require(id3, i3, 1);

        i->send_cancel();
        require_cancel(id1, i);

        require(id2, i2, 1);
        require(id3, i3, 1);
        i2->cnt_destroy = &nr_instruction_freed;
        i2->send_cancel();
        require_cancel(id2, i2);
        require(id3, i3, 1);

        mock_loop::get().dispatch_idle();
        REQUIRE(nr_instruction_freed == 2);
        require(id3, i3, 2);
    }

    SUBCASE("Aggregation of pending transactions")
    {
        auto tx2 = transaction_t::create();
        auto i2  = new mock_instruction_t("a");
        tx2->add_instruction(instruction_uptr_t(i2));

        auto tx3 = transaction_t::create();
        auto i31 = new mock_instruction_t("a");
        auto i32 = new mock_instruction_t("b");
        tx3->add_instruction(instruction_uptr_t(i31));
        tx3->add_instruction(instruction_uptr_t(i32));

        auto tx4 = transaction_t::create();
        auto i4  = new mock_instruction_t("b");
        tx4->add_instruction(instruction_uptr_t(i4));

        auto tx5 = transaction_t::create();
        auto i51 = new mock_instruction_t("c");
        auto i52 = new mock_instruction_t("a");
        tx5->add_instruction(instruction_uptr_t(i51));

        wf::signal_connection_t on_pending_add_a = [&] (wf::signal_data_t *data)
        {
            auto ev = static_cast<pending_signal*>(data);
            if (ev->tx->get_objects().count("c") &&
                !ev->tx->get_objects().count("a"))
            {
                ev->tx->add_instruction(instruction_uptr_t(i52));
            }
        };
        manager.connect_signal("pending", &on_pending_add_a);

        auto id2 = manager.submit(std::move(tx2));
        auto id3 = manager.submit(std::move(tx3));

        REQUIRE(id2 == id3);
        mock_loop::get().dispatch_idle();

        require(id1, i, 2);
        require(id2, i2, 1);
        require(id3, i31, 1);
        require(id3, i32, 1);

        auto id4 = manager.submit(std::move(tx4));
        auto id5 = manager.submit(std::move(tx5));

        require(id1, i, 2);
        require(id2, i2, 1);
        require(id3, i31, 1);
        require(id3, i32, 1);
        require(id4, i4, 1);
        require(id5, i51, 1);
        require(id5, i52, 1);

        mock_loop::get().dispatch_idle();
        require(id1, i, 2);
        require(id2, i2, 1);

        SUBCASE("Cancelling mega transaction")
        {
            i2->send_cancel();
            // test only a few of the instructions
            require_cancel(id2, i2);
            require_cancel(id3, i32);
            require_cancel(id5, i51);
        }

        SUBCASE("Committing mega transaction")
        {
            i->send_cancel();
            mock_loop::get().dispatch_idle();

            // test only a few
            require(id2, i2, 2);
            require(id3, i31, 2);
            require(id5, i52, 2);
        }
    }
}
