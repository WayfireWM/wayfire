#include <wayfire/debug.hpp>
#include <iostream>
#include "transaction-priv.hpp"

namespace wf
{
namespace txn
{
class transaction_manager_t::impl
{
  public:
    uint64_t submit(transaction_uptr_t tx)
    {
        auto tx_impl = dynamic_cast<transaction_impl_t*>(tx.get());

        pending_signal ev(tx);
        while (tx_impl->is_dirty())
        {
            tx_impl->clear_dirty();

            transaction_manager_t::get().emit_signal("pending", &ev);
            for (auto view : tx_impl->get_views())
            {
                view->emit_signal("transaction-pending", &ev);
            }
        }

        (void)tx.release();
        tx_impl->set_id(free_id);
        ++free_id;

        pending_idle.push_back(transaction_iuptr_t(tx_impl));
        return tx_impl->get_id();
    }

  private:
    uint64_t free_id = 0;

    // Transactions will be scheduled on next idle
    std::vector<transaction_iuptr_t> pending_idle;

    // Committed transactions
    std::vector<transaction_iuptr_t> committed;

    // Transaction thath pending transactions with conflicts are merged in
    transaction_iuptr_t mega_transaction;
};

transaction_manager_t& transaction_manager_t::get()
{
    static transaction_manager_t manager;
    return manager;
}

uint64_t transaction_manager_t::submit(transaction_uptr_t tx)
{
    return priv->submit(std::move(tx));
}

transaction_manager_t::transaction_manager_t()
{
    this->priv = std::make_unique<impl>();
}

transaction_manager_t::~transaction_manager_t() = default;
} // namespace txn
}
