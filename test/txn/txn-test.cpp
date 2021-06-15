#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../src/core/transaction/transaction-priv.hpp"
#include "mock-instruction.hpp"
#include "../mock-core.hpp"

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

    tx_ab->set_pending();
    check_instructions(1, 0, 0);

    tx_ab->commit();
    check_instructions(1, 1, 0);

    tx_ab->apply();
    check_instructions(1, 1, 1);
}
