#include <memory>
#include <wayfire/txn/transaction-manager.hpp>
#include "transaction-manager-impl.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/txn/transaction.hpp"

wf::txn::transaction_manager_t::transaction_manager_t()
{
    this->priv = std::make_unique<impl>();
}

wf::txn::transaction_manager_t::~transaction_manager_t() = default;

void wf::txn::transaction_manager_t::schedule_transaction(wf::txn::transaction_uptr tx)
{
    new_transaction_signal ev;
    ev.tx = tx.get();
    this->emit(&ev);
    priv->schedule_transaction(std::move(tx));
}

void wf::txn::transaction_manager_t::schedule_object(transaction_object_sptr object)
{
    auto tx = wf::txn::transaction_t::create();
    tx->add_object(std::move(object));
    schedule_transaction(std::move(tx));
}

template<class T>
static bool is_contained(const std::vector<T>& objs, const T& object)
{
    return std::find(objs.begin(), objs.end(), object) != objs.end();
}

bool wf::txn::transaction_manager_t::is_object_pending(transaction_object_sptr object) const
{
    return std::any_of(this->priv->pending.begin(), this->priv->pending.end(), [&] (auto& pending)
    {
        return is_contained(pending->get_objects(), object);
    });
}

bool wf::txn::transaction_manager_t::is_object_committed(transaction_object_sptr object) const
{
    return std::any_of(this->priv->committed.begin(), this->priv->committed.end(), [&] (auto& committed)
    {
        return is_contained(committed->get_objects(), object);
    });
}
