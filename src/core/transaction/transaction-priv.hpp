#pragma once

#include <map>
#include <wayfire/transaction/transaction.hpp>
#include <wayfire/util.hpp>

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

enum transaction_state_t
{
    NEW,
    PENDING,
    COMMITTED,
    DONE,
};

/**
 * Emits done/cancel if an instruction does so.
 */
class transaction_impl_t : public transaction_t, public signal_provider_t
{
  public:
    transaction_impl_t();

    /**
     * Set all instructions as pending.
     * Transaction state moves from NEW to PENDING
     */
    void set_pending();

    /**
     * Commit all instructions in the transaction.
     * Transaction state changes from PENDING to COMMITTED.
     */
    void commit();

    /**
     * Apply all instructions in the transaction.
     * Transaction state changes from COMMITTED to DONE.
     */
    void apply();

    /**
     * Move instructions from the other transaction to this,
     * thereby destroying the other transaction.
     *
     * Only NEW transactions can be merged into NEW or PENDING transactions.
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

    /**
     * Set the ID.
     */
    void set_id(uint32_t id)
    {
        this->id = id;
    }

    uint32_t get_id() const
    {
        return this->id;
    }

    transaction_state_t get_state() const;

  private:
    uint32_t id;
    int32_t instructions_done = 0;

    transaction_state_t state = NEW;
    std::vector<instruction_uptr_t> instructions;

    wf::signal_connection_t on_instruction_cancel;
    wf::signal_connection_t on_instruction_ready;

    wf::wl_timer commit_timeout;
    void emit_done(transaction_end_state_t end_state);
};
}
}
