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

        // We first set id to the transaction.
        // It may be merged into the mega transaction later.
        set_id(tx_impl);

        LOGC(TXN, "New transaction ", tx_impl->get_id());
        tx_impl->set_pending();
        tx_impl->connect_signal("done", &on_tx_done);
        collect_instructions(tx_impl);

        auto tx_iuptr = transaction_iuptr_t(tx_impl);
        if (is_conflict(tx_iuptr))
        {
            LOGC(TXN, "Merging into mega transaction");
            if (mega_transaction)
            {
                mega_transaction->merge(std::move(tx_iuptr));
            } else
            {
                mega_transaction = std::move(tx_iuptr);
            }

            return mega_transaction->get_id();
        }

        pending_idle.push_back(std::move(tx_iuptr));
        // Schedule for running later
        idle_commit.run_once();

        return tx_impl->get_id();
    }

  private:
    uint64_t free_id = 0;

    void set_id(transaction_impl_t *tx)
    {
        tx->set_id(free_id);
        ++free_id;
    }

    void collect_instructions(transaction_impl_t *tx)
    {
        pending_signal ev;
        ev.tx = {tx};
        while (tx->is_dirty())
        {
            tx->clear_dirty();
            emit_signal("pending", &ev);
        }
    }

    // Transactions that will be committed on next idle
    std::vector<transaction_iuptr_t> pending_idle;

    // Check whether tx has a conflict with an already scheduled transaction.
    bool is_conflict(const transaction_iuptr_t& tx,
        const transaction_iuptr_t& scheduled)
    {
        if (!scheduled)
        {
            return false;
        }

        if ((scheduled->get_state() != TXN_COMMITTED) &&
            (scheduled->get_state() != TXN_PENDING))
        {
            // Transaction already DONE, TIMED_OUT or CANCELLED
            return false;
        }

        return scheduled->does_intersect(*tx);
    }

    // Check whether a transaction has a conflict with a pending or committed
    // transactions
    bool is_conflict(const transaction_iuptr_t& tx)
    {
        if (is_conflict(tx, mega_transaction))
        {
            return true;
        }

        bool with_pending = std::any_of(pending_idle.begin(), pending_idle.end(),
            [&tx, this] (const auto& ptx) { return is_conflict(tx, ptx); });

        bool with_committed = std::any_of(committed.begin(), committed.end(),
            [&tx, this] (const auto& ctx) { return is_conflict(tx, ctx); });

        return with_pending || with_committed;
    }

    wf::wl_idle_call idle_commit;
    wf::wl_idle_call::callback_t idle_commit_handler = [=] ()
    {
        const auto& try_commit = [=] (transaction_iuptr_t& tx)
        {
            if (!tx || (tx->get_state() != TXN_PENDING))
            {
                return false;
            }

            bool can_commit = std::none_of(committed.begin(), committed.end(),
                [&tx, this] (const auto& ctx) { return is_conflict(tx, ctx); });

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

        try_commit(mega_transaction);
    };

    wf::wl_idle_call idle_cleanup;
    wf::wl_idle_call::callback_t idle_cleanup_handler = [=] ()
    {
        const auto& is_done = [] (const transaction_iuptr_t& tx)
        {
            return (tx->get_state() != TXN_COMMITTED) &&
                   (tx->get_state() != TXN_PENDING);
        };

        auto it = std::remove_if(committed.begin(), committed.end(), is_done);
        committed.erase(it, committed.end());

        it = std::remove_if(pending_idle.begin(), pending_idle.end(), is_done);
        pending_idle.erase(it, pending_idle.end());

        if (mega_transaction && is_done(mega_transaction))
        {
            mega_transaction.reset();
        }
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
          // fallthrough
          case TXN_TIMED_OUT:
            LOGC(TXN, "Applying transaction ", tx->get_id(),
                " (timeout: ", ev->state == TXN_TIMED_OUT, ")");
            emit_signal("ready", &emit_ev);
            tx->apply();
            emit_signal("done", &emit_ev);

            // NB: we need to first commit the next instruction, and clean up
            // after that. This way, surface locks can be transferred from the
            // previous to the next transaction.
            idle_commit.run_once();
            idle_cleanup.run_once();
            break;

          case TXN_CANCELLED:
            LOGC(TXN, "Transaction ", tx->get_id(), " cancelled");
            emit_signal("done", &emit_ev);

            // NB: we need to first commit the next instruction, and clean up
            // after that. This way, surface locks can be transferred from the
            // previous to the next transaction.
            idle_commit.run_once();
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
            // tx may be null if we're in the process of committing it
            if (tx && (tx->get_id() == id))
            {
                return tx;
            }
        }

        for (auto& tx : committed)
        {
            if (tx && (tx->get_id() == id))
            {
                return tx;
            }
        }

        assert(false);
    }

    void do_commit(transaction_iuptr_t tx)
    {
        LOGC(TXN, "Committing transaction ", tx->get_id());
        committed.push_back(std::move(tx));
        committed.back()->commit();
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

    // Enable logs for tests
    wf::log::enabled_categories.set(
        (size_t)wf::log::logging_category::TXN, 1);
    wf::log::initialize_logging(std::cout, wf::log::LOG_LEVEL_DEBUG,
        wf::log::LOG_COLOR_MODE_OFF);

    LOGD("Refreshed transaction manager");
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
