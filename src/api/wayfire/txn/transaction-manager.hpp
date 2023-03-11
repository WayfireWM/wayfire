#pragma once

#include "wayfire/signal-provider.hpp"
#include "wayfire/txn/transaction-object.hpp"
#include <wayfire/txn/transaction.hpp>

namespace wf
{
namespace txn
{
/*
 * The transaction manager keeps track of all committed and pending transactions and ensures that there is at
 * most one committed transaction for a given object.
 *
 * In order to do ensure correct ordering of transactions, it keeps a list of pending transactions. The first
 * transaction is committed as soon as there are no committed transactions with the same objects. In addition,
 * any new transactions which are not immediately committed but work with the same objects are coalesced
 * together. For example, if there are two transactions, one for objects A, for objects B,C and a third
 * transaction for objects A,B comes in, then all these three are merged together. This merging is done to
 * avoid sending excessive configure events to clients - for example during an interactive resize.
 */
class transaction_manager_t : public signal::provider_t
{
  public:
    transaction_manager_t();
    ~transaction_manager_t();

    /**
     * Add a new transaction to the list of scheduled transactions. The transaction might be merged with
     * other transactions which came before or after it, according to the coalescing schema described above.
     *
     * Note that a transaction will never be started immediately. Instead, it will be started on the next idle
     * event of the event loop.
     */
    void schedule_transaction(transaction_uptr tx);

    /**
     * A convenience function to create a transaction for a single object and schedule it via
     * schedule_transaction().
     */
    void schedule_object(transaction_object_sptr object);

    /**
     * Check whether there is a pending transaction for the given object.
     */
    bool is_object_pending(transaction_object_sptr object) const;

    /**
     * Check whether there is a committed transaction for the given object.
     */
    bool is_object_committed(transaction_object_sptr object) const;

    struct impl;
    std::unique_ptr<impl> priv;
};

/**
 * The new-transaction signal is emitted before a new transaction is added to the transaction manager (e.g.
 * at the beginning of schedule_transaction()). The transaction may be merged into another transaction before
 * it is actually executed.
 */
struct new_transaction_signal
{
    transaction_t *tx;
};
}
}
