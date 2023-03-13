#pragma once
#include <wayfire/txn/transaction-object.hpp>
#include <wayfire/debug.hpp>
#include <iostream>

class txn_test_object_t : public wf::txn::transaction_object_t
{
  public:
    int number_committed = 0;
    int number_applied   = 0;
    std::function<void()> apply_callback;

    bool autoready;

    txn_test_object_t(bool autocommit)
    {
        this->autoready = autocommit;
    }

    void commit() override
    {
        number_committed++;
        if (autoready)
        {
            emit_ready();
        }
    }

    void apply() override
    {
        number_applied++;
        if (apply_callback)
        {
            apply_callback();
        }
    }

    void emit_ready()
    {
        wf::txn::object_ready_signal ev;
        ev.self = this;
        this->emit(&ev);
    }
};

inline void setup_wayfire_debugging_state()
{
    wf::log::initialize_logging(std::cout, wf::log::LOG_LEVEL_DEBUG, wf::log::LOG_COLOR_MODE_ON);
    wf::log::enabled_categories.set((size_t)wf::log::logging_category::TXN, 1);
    wf::log::enabled_categories.set((size_t)wf::log::logging_category::TXNI, 1);
    // Set wl_idle_call's loop to a fake loop so that the test doesn't crash when using signals which in turn
    // use safe_list_t which needs wl_idle_calls.
    wf::wl_idle_call::loop = wl_event_loop_create();
}
