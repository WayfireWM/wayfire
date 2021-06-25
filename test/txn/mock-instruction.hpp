#include <wayfire/transaction/instruction.hpp>

class mock_instruction_t : public wf::txn::instruction_t
{
  public:
    static inline wf::txn::instruction_uptr_t get(std::string object)
    {
        return std::make_unique<mock_instruction_t>(object);
    }

    std::string object;
    mock_instruction_t(std::string object = "")
    {
        this->object = object;
    }

    int pending   = 0;
    int committed = 0;
    int applied   = 0;

    std::string get_object() override
    {
        return object;
    }

    void set_pending() override
    {
        ++pending;
    }

    void commit() override
    {
        ++committed;
    }

    void apply() override
    {
        ++applied;
    }

    void send_ready()
    {
        wf::txn::instruction_ready_signal data;
        data.instruction = {this};
        this->emit_signal("ready", &data);
    }

    void send_cancel()
    {
        wf::txn::instruction_cancel_signal data;
        data.instruction = {this};
        this->emit_signal("cancel", &data);
    }
};
