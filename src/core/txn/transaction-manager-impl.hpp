#include "wayfire/signal-provider.hpp"
#include "wayfire/txn/transaction.hpp"
#include <algorithm>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/debug.hpp>

static bool transactions_intersect(const wf::txn::transaction_uptr& a, const wf::txn::transaction_uptr& b)
{
    const auto& obj_a = a->get_objects();
    const auto& obj_b = b->get_objects();

    return std::any_of(obj_a.begin(), obj_a.end(), [&] (const wf::txn::transaction_object_sptr& x)
    {
        return std::find(obj_b.begin(), obj_b.end(), x) != obj_b.end();
    });
}

struct wf::txn::transaction_manager_t::impl
{
    impl()
    {
        idle_clear_done.set_callback([=] () { done.clear(); });
    }

    void schedule_transaction(transaction_uptr tx)
    {
        LOGC(TXN, "Scheduling transaction ", tx.get());

        // Step 1: add any objects which are directly or indirectly connected to the objects in tx
        coalesce_transactions(tx);

        // Step 2: remove any transactions we don't need anymore, as their objects were added to tx
        remove_conflicts(tx);

        // Step 3: schedule tx for execution. At this point, there are no conflicts in all pending txs
        pending.push_back(std::move(tx));
        consider_commit();
    }

    void coalesce_transactions(const transaction_uptr& tx)
    {
        while (true)
        {
            const size_t start_size = tx->get_objects().size();
            for (auto& existing : pending)
            {
                if (transactions_intersect(existing, tx))
                {
                    for (auto& obj : existing->get_objects())
                    {
                        tx->add_object(obj);
                    }
                }
            }

            if (start_size == tx->get_objects().size())
            {
                // No new objects were added in the last iteration => done
                break;
            }
        }
    }

    void remove_conflicts(const transaction_uptr& tx)
    {
        auto it = std::remove_if(pending.begin(), pending.end(), [&] (const transaction_uptr& existing)
        {
            return transactions_intersect(existing, tx);
        });
        pending.erase(it, pending.end());
    }

    // Try to commit as many transactions as possible
    void consider_commit()
    {
        idle_clear_done.run_once();

        // Implementation note: the merging strategy guarantees no conflicts between pending transactions,
        // so we just need to check conflicts between committed and pending
        for (size_t idx = 0; idx < pending.size();)
        {
            if (can_commit_transaction(pending[idx]))
            {
                auto tx = std::move(pending[idx]);
                pending.erase(pending.begin() + idx);
                do_commit(std::move(tx));
                // Note: the container may change after this operation, because some objects emit ready
                // directly inside commit().
            } else
            {
                ++idx;
            }
        }
    }

    bool can_commit_transaction(const transaction_uptr& tx)
    {
        return std::none_of(committed.begin(), committed.end(), [&] (const transaction_uptr& comm)
        {
            return transactions_intersect(tx, comm);
        });
    }

    void do_commit(transaction_uptr tx)
    {
        tx->connect(&on_tx_apply);
        committed.push_back(std::move(tx));
        // Note: this might immediately trigger tx_apply if all objects are already ready!
        committed.back()->commit();
    }

    std::vector<transaction_uptr> done; // Temporary storage for transactions which are complete
    std::vector<transaction_uptr> committed;
    std::vector<transaction_uptr> pending;
    wf::wl_idle_call idle_clear_done;

    wf::signal::connection_t<transaction_applied_signal> on_tx_apply = [&] (transaction_applied_signal *ev)
    {
        // Move transactions which are done from committed to done.
        // They will be freed on next idle.
        auto it = std::find_if(committed.begin(), committed.end(), [&] (auto& existing)
        {
            return existing.get() == ev->self;
        });

        wf::dassert(it != committed.end(), "Transaction not found in committed list");

        done.push_back(std::move(*it));
        committed.erase(it);
        consider_commit();
    };
};
