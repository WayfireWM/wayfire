#pragma once

#include <wayfire/transaction/instruction.hpp>
#include <wayfire/view.hpp>
#include <memory>
#include <set>

namespace wf
{
namespace txn
{
/**
 * Describes the state of a transaction at the end of its lifetime.
 */
enum transaction_end_state_t
{
    /**
     * Transaction was cancelled because one of the participating objects
     * was destroyed.
     */
    TXN_CANCELLED,
    /**
     * Transaction has timed out because instruction took too long to commit.
     */
    TXN_TIMED_OUT,
    /**
     * Transaction was successfully applied.
     */
    TXN_APPLIED,
};

/**
 * name: done
 * on: transaction-manager
 * when: Whenever a transaction has been applied or cancelled.
 * argument: unused
 */
struct done_signal_t
{
    /** The ID of the transaction. */
    uint64_t id;

    /** The state of the transaction. */
    transaction_end_state_t state;
};

class transaction_t;
using transaction_uptr_t = std::unique_ptr<transaction_t>;

/**
 * transaction_t represents a collection of changes to views' states which
 * are applied atomically. The individual changes are called instructions.
 *
 * The transaction lifetime is as follows:
 *
 * - NEW: transaction has been created, but not submitted to core. In this
 *   state, transactions can live indefinitely and have no effect whatsoever.
 *   Generally, a transaction should be submitted as soon as possible, to avoid
 *   the case where it contains a dangling view (this case causes a crash).
 *
 * - PENDING: transaction has been submitted to core via
 *   wf::compositor_core_t::submit().  Instructions are not yet sent to clients.
 *
 * - COMMITTED: instructions have been sent to client, and Wayfire is waiting
 *   for a response.
 *
 * Transactions are moved from PENDING to COMMITTED automatically.
 * This is possible as soon as there are no COMMITTED transactions which affect
 * the same views.  Otherwise, all PENDING transactions are merged together into
 * a single PENDING transaction.
 *
 * Destruction of transactions:
 *
 * - A pending or committed transaction may fail at any time, for ex. if a view
 *   in it is closed by the client.
 * - A committed transaction may time out because clients are too slow to
 *   respond.
 * - A committed transaction may succeed if all clients update their surfaces on
 *   time.
 *
 * In all of these three cases, 'done' signal is emitted from the transaction
 * manager, with information on why the transaction has ended.
 */
class transaction_t
{
  public:
    /**
     * Create a NEW empty transaction.
     *
     * Note: this is the only valid way to create transactions which can be
     * submitted to Wayfire's core for processing.
     */
    static transaction_uptr_t create();

    /**
     * Add new instructions for a given object.
     *
     * The object identifier is used for determining whether two transactions
     * can be committed in parallel (only if they touch separate objects).
     *
     * By default, views and outputs have unique IDs which are stringified
     * and used as their object identifier.
     *
     * @param object The object identifier.
     * @param instr The instruction for the object.
     */
    virtual void add_instruction(instruction_uptr_t instr) = 0;

    /**
     * Get a list of all objects which are influenced by this transaction.
     */
    virtual std::set<std::string> get_objects() const = 0;

    /**
     * Get a list of all views influenced by this transaction.
     */
    virtual std::set<wayfire_view> get_views() const = 0;

    transaction_t(const transaction_t& other) = delete;
    transaction_t(transaction_t&& other) = delete;
    transaction_t& operator =(const transaction_t& other) = delete;
    transaction_t& operator =(transaction_t&& other) = delete;

    virtual ~transaction_t() = default;

  protected:
    transaction_t() = default;
};

/**
 * A class which holds all active (pending/committed) transactions.
 * It is responsible for merging pending transactions, committing and finalizing
 * transactions.
 */
class transaction_manager_t : public signal_provider_t
{
  public:
    /**
     * Get the single global instance of the transaction manager.
     */
    static transaction_manager_t& get();

    /**
     * Submit a new transaction.
     * It becomes pending and is committed as soon as possible.
     *
     * @return The ID of the transaction. Note that the transaction may be
     *   merged into an existing pending transaction, in which case, the
     *   old transaction ID will be returned.
     */
    uint64_t submit(transaction_uptr_t tx);

    // Implementation details
    class impl;
    std::unique_ptr<impl> priv;

  private:
    transaction_manager_t();
    ~transaction_manager_t();

    transaction_manager_t(const transaction_manager_t& other) = delete;
    transaction_manager_t(transaction_manager_t&& other) = delete;
    transaction_manager_t& operator =(const transaction_manager_t& other) = delete;
    transaction_manager_t& operator =(transaction_manager_t&& other) = delete;
};
}
}
