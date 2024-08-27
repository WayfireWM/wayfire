#pragma once

#include <wayfire/signal-provider.hpp>
#include <string>
#include <memory>

namespace wf
{
namespace txn
{
/**
 * A transaction object participates in the transactions system.
 *
 * Transaction objects usually have double-buffered state, which may not be applicable immediately, that is,
 * when a state change is requested, it takes some time until the changes can be applied. Sometimes, multiple
 * such objects are updated together in a single transaction, in which case the changes are to be seen as
 * atomic across all participating objects.
 *
 * The typical example of transaction objects are toplevels, where changing for example the size of the
 * toplevel requires cooperation from the client, and therefore cannot be done instantaneously.
 *
 * When speaking about transaction objects, they usually have three different types of state: current,
 * committed and pending. Current state is what the object currently is configured as, committed state is a
 * state which will soon be current (e.g. changes are underway), and pending are changes which have been
 * planned for the future, but execution has not started yet.
 */
class transaction_object_t : public signal::provider_t
{
  public:
    /**
     * Get a string representing the transaction object. Used for debugging purposes.
     */
    virtual std::string stringify() const;

    /**
     * Make the pending state committed.
     * This function is called when a transaction is committed.
     */
    virtual void commit() = 0;

    /**
     * Make the committed state current.
     * This function is called when all transaction objects in a transaction are ready to apply the committed
     * state.
     */
    virtual void apply() = 0;

    virtual ~transaction_object_t() = default;
};

using transaction_object_sptr = std::shared_ptr<transaction_object_t>;

/**
 * A signal emitted on a transaction_object_t to indicate that it is ready to be applied.
 */
struct object_ready_signal
{
    transaction_object_t *self;
};

/**
 * Emit the object-ready signal on the given object.
 */
void emit_object_ready(wf::txn::transaction_object_t *obj);
}
}
