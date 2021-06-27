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

    auto& manager = transaction_manager_t::get();

    auto id  = manager.submit(std::move(tx));
    auto id2 = manager.submit(std::move(tx2));

    REQUIRE(id == tx_raw->get_id());
    REQUIRE(id2 == tx2_raw->get_id());
    REQUIRE(id != id2);
}

TEST_CASE("Submit and extend transaction")
{
    auto& manager = transaction_manager_t::get();
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
}
