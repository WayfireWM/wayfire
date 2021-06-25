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
 * name: ready
 * on: instruction
 * when: Emitted whenever the instruction is ready to be applied.
 */
using instruction_ready_signal = _instruction_signal;

/**
 * name: cancel
 * on: instruction
 * when: Emitted whenever the instruction can no longer be applied.
 */
using instruction_cancel_signal = _instruction_signal;

/**
 * A single instruction which is part of a transaction.
 * The instruction can change one or more states in Wayfire.
 *
 * The instruction lifetime is as follows:
 *
 * 1. Instruction is created and added to a transaction.
 * 2. Instruction is marked as pending. This means its transaction has been
 *   added to the queue of pending transactions, and will eventually be
 *   applied.
 * 3. Instruction is committed when its transaction is committed. This means
 *   that Wayfire is waiting for clients or other entities to become ready to
 *   apply the transaction.
 * 4. Instruction becomes ready (e.g. client responds with a new buffer), and
 *   'ready' is emitted.
 * 5. Instruction is applied when all instructions in its transaction are
 *   ready. Note that this can happen even before the instruction is ready, in
 *   case of a timeout. In this case, the instruction implementation determines
 *   the best course of action.
 *
 * If at any point the instruction becomes impossible to apply (e.g. view is
 * unmapped, output becomes destroyed, etc.), the 'cancel' signal should be
 * emitted on the instruction.
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
