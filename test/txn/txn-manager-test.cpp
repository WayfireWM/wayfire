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
}
