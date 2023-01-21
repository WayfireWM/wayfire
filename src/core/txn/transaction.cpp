#include "wayfire/txn/transaction-object.hpp"
#include <wayfire/txn/transaction.hpp>
#include <sstream>
#include <wayfire/debug.hpp>

std::string wf::txn::transaction_object_t::stringify() const
{
    std::ostringstream out;
    out << this;
    return out.str();
}

wf::txn::transaction_t::transaction_t(uint64_t timeout)
{
    this->timeout = timeout;

    this->on_object_ready = [=] (object_ready_signal *ev)
    {
        this->count_ready_objects++;
        LOGC(TXNI, "Transaction ", this, " object ", ev->self->stringify(), " became ready (",
            count_ready_objects, "/", this->objects.size(), ")");

        if (count_ready_objects == (int)this->objects.size())
        {
            apply(false);
        }
    };
}

const std::vector<wf::txn::transaction_object_sptr>& wf::txn::transaction_t::get_objects() const
{
    return this->objects;
}

void wf::txn::transaction_t::add_object(transaction_object_sptr object)
{
    LOGC(TXNI, "Transaction ", this, " add object ", object->stringify());
    auto it = std::find(objects.begin(), objects.end(), object);
    if (it == objects.end())
    {
        objects.push_back(object);
        object->connect(&on_object_ready);
    }
}

void wf::txn::transaction_t::commit(timer_setter_t timer_setter)
{
    LOGC(TXN, "Committing transaction ", this, " with timeout ", this->timeout);
    for (auto& obj : this->objects)
    {
        obj->commit();
    }

    timer_setter(this->timeout, [=] ()
    {
        apply(true);
        return false;
    });
}

void wf::txn::transaction_t::apply(bool did_timeout)
{
    LOGC(TXN, "Applying transaction ", this, " timed_out: ", did_timeout);
    for (auto& obj : this->objects)
    {
        obj->apply();
    }

    transaction_applied_signal ev;
    ev.self = this;
    ev.timed_out = did_timeout;
    this->emit(&ev);
}
