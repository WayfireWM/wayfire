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
    impl()
    {
        idle_commit.set_callback(idle_commit_handler);
        idle_cleanup.set_callback(idle_cleanup_handler);
    }

    uint64_t submit(transaction_uptr_t tx)
    {
        auto tx_impl = dynamic_cast<transaction_impl_t*>(tx.release());
        tx_impl->set_id(free_id);
        ++free_id;

        tx_impl->set_pending();
        pending_signal ev;
        ev.tx = {tx_impl};
        while (tx_impl->is_dirty())
        {
            tx_impl->clear_dirty();
            emit_signal("pending", &ev);
        }

        pending_idle.push_back(transaction_iuptr_t(tx_impl));
        // Schedule for running later
        idle_commit.run_once();

        return tx_impl->get_id();
    }

  private:
    uint64_t free_id = 0;

    // Transactions that will be committed on next idle
    std::vector<transaction_iuptr_t> pending_idle;

    wf::wl_idle_call idle_commit;
    wf::wl_idle_call::callback_t idle_commit_handler = [=] ()
    {
        const auto& try_commit = [=] (transaction_iuptr_t& tx)
        {
            bool can_commit = std::none_of(committed.begin(), committed.end(),
                [&tx] (const auto& ctx)
            {
                // Filter out only in-progress transactions.
                // The others are already DONE or CANCELLED.
                return (tx->get_state() == TXN_COMMITTED) &&
                tx->does_intersect(*ctx);
            });

            if (can_commit)
            {
                do_commit(std::move(tx));
                return true;
            }

            return false;
        };

        for (auto it = pending_idle.begin(); it != pending_idle.end();)
        {
            if (try_commit(*it))
            {
                it = pending_idle.erase(it);
            } else
            {
                ++it;
            }
        }

        if (mega_transaction)
        {
            try_commit(mega_transaction);
        }
    };

    wf::wl_idle_call idle_cleanup;
    wf::wl_idle_call::callback_t idle_cleanup_handler = [=] ()
    {
        const auto& is_done = [] (const transaction_iuptr_t& tx)
        {
            return tx->get_state() != TXN_COMMITTED;
        };

        auto it = std::remove_if(committed.begin(), committed.end(), is_done);
        committed.erase(it, committed.end());
    };

    // Committed transactions
    std::vector<transaction_iuptr_t> committed;
    wf::signal_connection_t on_tx_done = [=] (wf::signal_data_t *data)
    {
        auto ev  = static_cast<priv_done_signal*>(data);
        auto& tx = find_transaction(ev->id);

        ready_signal emit_ev;
        emit_ev.tx = {tx};

        switch (ev->state)
        {
          case TXN_READY:
            emit_signal("ready", &emit_ev);
            tx->apply();
            emit_signal("done", &emit_ev);
            idle_cleanup.run_once();
            break;

          default:
            assert(false);
        }
    };

    transaction_iuptr_t& find_transaction(uint64_t id)
    {
        if (mega_transaction && (id == mega_transaction->get_id()))
        {
            return mega_transaction;
        }

        for (auto& tx : pending_idle)
        {
            if (tx->get_id() == id)
            {
                return tx;
            }
        }

        for (auto& tx : committed)
        {
            if (tx->get_id() == id)
            {
                return tx;
            }
        }

        assert(false);
    }

    void do_commit(transaction_iuptr_t tx)
    {
        tx->commit();
        tx->connect_signal("done", &on_tx_done);
        committed.push_back(std::move(tx));
    }

    void emit_signal(const std::string& name, transaction_signal *data)
    {
        transaction_manager_t::get().emit_signal(name, data);
        for (auto view : data->tx->get_views())
        {
            view->emit_signal("transaction-" + name, data);
        }
    }

    // Transaction that pending transactions with conflicts are merged in
    transaction_iuptr_t mega_transaction;
};

transaction_manager_t& transaction_manager_t::get()
{
    static transaction_manager_t manager;
    return manager;
}

transaction_manager_t& get_fresh_transaction_manager()
{
    auto& mgr = transaction_manager_t::get();
    mgr.priv = std::make_unique<transaction_manager_t::impl>();
    return mgr;
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
