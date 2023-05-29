#pragma once

#include "wayfire/signal-provider.hpp"
#include "wayfire/util.hpp"
#include <wayfire/txn/transaction-object.hpp>

namespace wf
{
namespace txn
{
/**
 * A transaction contains one or more transaction objects whose state should be applied atomically, that is,
 * changes to the objects should be applied only after all the objects are ready to apply the changes.
 */
class transaction_t : public signal::provider_t
{
  public:
    using timer_setter_t = std::function<void (uint64_t, wf::wl_timer<false>::callback_t)>;

    /**
     * Create a new transaction.
     *
     * @param timeout The timeout for the transaction in milliseconds after it is committed.
     *   -1 means that core should pick a default timeout.
     */
    static std::unique_ptr<transaction_t> create(int64_t timeout = -1);

    /**
     * Create a new empty transaction.
     *
     * @param timer A function used to set timeout callbacks at runtime.
     * @param timeout The maximal duration, in milliseconds, to wait for transaction objects to become ready.
     *   When the timeout is reached, all committed state is applied.
     */
    transaction_t(uint64_t timeout, timer_setter_t timer_setter);

    /**
     * Add a new object to the transaction. If the object was already part of it, this is no-op.
     */
    void add_object(transaction_object_sptr object);

    /**
     * Get a list of all the objects currently part of the transaction.
     */
    const std::vector<transaction_object_sptr>& get_objects() const;

    /**
     * Commit the transaction, that is, commit the pending state of all participating objects.
     * As soon as all objects are ready or the transaction times out, the state will be applied.
     */
    void commit();

    virtual ~transaction_t() = default;

  private:
    std::vector<transaction_object_sptr> objects;
    int count_ready_objects = 0;
    uint64_t timeout;
    timer_setter_t timer_setter;

    void apply(bool did_timeout);
    wf::signal::connection_t<object_ready_signal> on_object_ready;
};

using transaction_uptr = std::unique_ptr<transaction_t>;

/**
 * A signal emitted on a transaction as soon as it has been applied.
 */
struct transaction_applied_signal
{
    transaction_t *self;

    // Set to true if the transaction timed out and the desired object state may not have been achieved.
    bool timed_out;
};
}
}
