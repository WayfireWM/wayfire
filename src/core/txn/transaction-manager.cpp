#include <memory>
#include <wayfire/txn/transaction-manager.hpp>
#include "transaction-manager-impl.hpp"
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
