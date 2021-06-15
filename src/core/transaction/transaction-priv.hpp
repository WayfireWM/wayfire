#pragma once

#include <map>
#include <wayfire/transaction/transaction.hpp>

namespace wf
{
namespace txn
{
class transaction_impl_t;
using transaction_iuptr_t = std::unique_ptr<transaction_impl_t>;

class transaction_manager_t::impl
{
  public:

  private:
    /**
     * The single pending transaction, if it exists.
     * Otherwise, nullptr.
     */
    transaction_iuptr_t pending;

    /**
     * A list of committed transactions.
     */
    std::vector<transaction_iuptr_t> committed;
};

/**
 * Emits done/cancel if an instruction does so.
 */
class transaction_impl_t : public transaction_t, public signal_provider_t
{
  public:
    /** Set all instructions as pending. */
    void set_pending();

    /** Commit all instructions in the transaction. */
    void commit();

    /** Apply all instructions in the transaction. */
    void apply();

    /**
     * Move instructions from the other transaction to this,
     * thereby destroying the other transaction.
     */
    void merge(transaction_iuptr_t other);

    /**
     * Test whether instructions collide with each other (i.e have instructions
     * for the same objects).
     */
    bool does_intersect(const transaction_impl_t& other) const;

    void add_instruction(instruction_uptr_t instr) override;
    std::set<std::string> get_objects() const override;
    std::set<wayfire_view> get_views() const override;

  private:
    std::vector<instruction_uptr_t> instructions;
};
}
}
