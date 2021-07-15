#include <wayfire/debug.hpp>

#include "transaction-priv.hpp"
#include "../core-impl.hpp"

namespace wf
{
namespace txn
{
transaction_impl_t::transaction_impl_t()
{
    this->on_instruction_cancel.set_callback([=] (wf::signal_data_t*)
    {
        state = TXN_CANCELLED;
        emit_done(TXN_CANCELLED);
        commit_timeout.disconnect();
    });

    this->on_instruction_ready.set_callback([=] (wf::signal_data_t*)
    {
        ++instructions_done;
        if (instructions_done == (int32_t)instructions.size())
        {
            state = TXN_READY;
            emit_done(TXN_READY);
            commit_timeout.disconnect();
        }
    });
}

void transaction_impl_t::set_pending()
{
    assert(this->state == TXN_NEW);
    for (auto& i : this->instructions)
    {
        i->set_pending();
        i->connect_signal("cancel", &on_instruction_cancel);
    }

    this->state = TXN_PENDING;
}

void transaction_impl_t::commit()
{
    assert(this->state == TXN_PENDING);
    for (auto& i : this->instructions)
    {
        i->connect_signal("ready", &on_instruction_ready);
        i->commit();
    }

    this->state = TXN_COMMITTED;

    commit_timeout.set_timeout(100, [=] ()
    {
        state = TXN_TIMED_OUT;
        emit_done(TXN_TIMED_OUT);
        return false;
    });
}

void transaction_impl_t::apply()
{
    assert(this->state == TXN_READY || this->state == TXN_TIMED_OUT);
    for (auto& i : this->instructions)
    {
        i->apply();
    }

    this->state = TXN_APPLIED;
}

transaction_state_t transaction_impl_t::get_state() const
{
    return state;
}

void transaction_impl_t::merge(transaction_iuptr_t other)
{
    assert(other->get_state() == TXN_NEW || other->get_state() == TXN_PENDING);
    assert(state == TXN_NEW || state == TXN_PENDING);
    assert(!(state == TXN_NEW && other->get_state() == TXN_PENDING));

    for (auto& i : other->instructions)
    {
        add_instruction(std::move(i), other->get_state() == TXN_PENDING);
    }

    // drop other
}

bool transaction_impl_t::does_intersect(const transaction_impl_t& other) const
{
    auto objs = get_objects();
    auto other_objs = other.get_objects();

    std::vector<std::string> intersection;

    std::set_intersection(objs.begin(), objs.end(),
        other_objs.begin(), other_objs.end(), std::back_inserter(intersection));

    return !intersection.empty();
}

void transaction_impl_t::add_instruction(instruction_uptr_t instr)
{
    add_instruction(std::move(instr), false);
}

void transaction_impl_t::add_instruction(instruction_uptr_t instr,
    bool already_pending)
{
    assert(state == TXN_NEW || state == TXN_PENDING);

    if (state == TXN_PENDING)
    {
        instr->connect_signal("cancel", &on_instruction_cancel);
        if (!already_pending)
        {
            instr->set_pending();
        }
    }

    this->instructions.push_back(std::move(instr));
    this->dirty = true;
}

std::set<std::string> transaction_impl_t::get_objects() const
{
    std::set<std::string> objs;
    for (auto& i : instructions)
    {
        objs.insert(i->get_object());
    }

    return objs;
}

std::set<wayfire_view> transaction_impl_t::get_views() const
{
    std::set<wayfire_view> views;
    for (auto& i : instructions)
    {
        auto view = wf::get_core_impl().find_view(i->get_object());
        if (view)
        {
            views.insert(view);
        }
    }

    return views;
}

void transaction_impl_t::emit_done(transaction_state_t end_state)
{
    this->on_instruction_ready.disconnect();
    this->on_instruction_cancel.disconnect();

    priv_done_signal ev;
    ev.id    = this->get_id();
    ev.state = end_state;
    this->emit_signal("done", &ev);
}

transaction_uptr_t transaction_t::create()
{
    return std::make_unique<transaction_impl_t>();
}

bool transaction_impl_t::is_dirty() const
{
    return this->dirty;
}

void transaction_impl_t::clear_dirty()
{
    this->dirty = false;
}
} // namespace txn
}
