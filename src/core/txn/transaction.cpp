#include "wayfire/option-wrapper.hpp"
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

wf::txn::transaction_t::transaction_t(uint64_t timeout, timer_setter_t timer_setter)
{
    this->timeout = timeout;
    this->timer_setter = timer_setter;

    this->on_object_ready = [=] (object_ready_signal *ev)
    {
        this->count_ready_objects++;
        LOGC(TXNI, "Transaction ", this, " object ", ev->self->stringify(), " became ready (",
            count_ready_objects, "/", this->objects.size(), ")");

        wf::dassert(count_ready_objects <= (int)this->objects.size(), "object emitted ready multiple times?");
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
    auto it = std::find(objects.begin(), objects.end(), object);
    if (it == objects.end())
    {
        LOGC(TXNI, "Transaction ", this, " add object ", object->stringify());
        objects.push_back(object);
    }
}

void wf::txn::transaction_t::commit()
{
    LOGC(TXN, "Committing transaction ", this, " with timeout ", this->timeout);
    if (this->objects.empty())
    {
        // Empty transaction, directly ready.
        apply(false);
        return;
    }

    for (auto& obj : this->objects)
    {
        obj->connect(&on_object_ready);
        obj->commit();
    }

    timer_setter(this->timeout, [=] ()
    {
        if (count_ready_objects < (int)this->objects.size())
        {
            apply(true);
        }

        return false;
    });
}

void wf::txn::transaction_t::apply(bool did_timeout)
{
    on_object_ready.disconnect();

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

/**
 * A transaction which uses wl_timer for timeouts.
 */
class wayfire_default_transaction_t : public wf::txn::transaction_t
{
  public:
    wayfire_default_transaction_t(int64_t timeout) : transaction_t(timeout, get_timer_setter())
    {}

  private:
    wf::wl_timer<false> timer;
    timer_setter_t get_timer_setter()
    {
        return [this] (uint64_t timeout, wf::wl_timer<false>::callback_t cb)
        {
            timer.set_timeout(timeout, cb);
        };
    }
};

std::unique_ptr<wf::txn::transaction_t> wf::txn::transaction_t::create(int64_t timeout)
{
    if (timeout == -1)
    {
        static wf::option_wrapper_t<int> tx_timeout{"core/transaction_timeout"};
        timeout = tx_timeout;
    }

    return std::make_unique<wayfire_default_transaction_t>(timeout);
}

void wf::txn::emit_object_ready(wf::txn::transaction_object_t *obj)
{
    wf::txn::object_ready_signal data_ready;
    data_ready.self = obj;
    obj->emit(&data_ready);
    return;
}
