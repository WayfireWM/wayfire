#pragma once

#include <wayfire/object.hpp>

namespace wf
{
namespace txn
{
class instruction_t;
struct _instruction_signal : public wf::signal_data_t
{
    nonstd::observer_ptr<instruction_t> instruction;
};

/**
 * name: done
 * on: instruction
 * when: Emitted whenever the instruction is ready to be applied.
 */
using instruction_done_signal = _instruction_signal;

/**
 * A single instruction which is part of a transaction.
 * The instruction can change one or more states in Wayfire.
 *
 * Instruction lifetimes and states are similar to those of whole transactions.
 * An instruction can be in a pending state (hasn't been applied to anything),
 * or committed state (sent to clients but not applied yet).
 *
 * As soon as the transaction has been committed and it is ready to be applied,
 * it should emit the 'ready' signal.
 */
class instruction_t : public wf::signal_provider_t
{
  public:
    /**
     * @return The object this instruction is operating on.
     */
    virtual std::string get_object() = 0;

    /**
     * The instruction has been marked as pending.
     * This is useful for objects like views which keep track of current
     * and pending state.
     */
    virtual void set_pending()
    {}

    /**
     * Commit the instruction.
     * This usually involves sending configure events to client surfaces,
     * or other similar mechanism for non-view instructions.
     *
     * If there is nothing to commit, the instruction should still emit the
     * 'done' signal immediately.
     */
    virtual void commit() = 0;

    /**
     * Apply the instruction.
     * This involves actually manipulating the states of views, outputs, etc.
     * in order to achieve the effect of the instruction.
     * In this way the changes become visible to all plugins and to the user.
     *
     * NOTE: generally, when applying an instruction/transaction, only such
     * methods should be used which can be applied immediately and do not
     * trigger signals.
     */
    virtual void apply() = 0;
};

using instruction_uptr_t = std::unique_ptr<instruction_t>;
}
}
