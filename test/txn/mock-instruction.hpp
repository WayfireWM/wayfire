#include <wayfire/config/option-wrapper.hpp>
#include <wayfire/transaction/instruction.hpp>
#include <doctest/doctest.h>
#include <optional>
#include "../mock-core.hpp"

class mock_instruction_t : public wf::txn::instruction_t
{
  public:
    static inline wf::txn::instruction_uptr_t get(std::string object)
    {
        return std::make_unique<mock_instruction_t>(object);
    }

    bool ready_on_commit = false;
    int *cnt_destroy     = nullptr;
    std::optional<int> require_destroy_on_commit;

    std::string object;
    mock_instruction_t(std::string object = "")
    {
        this->object = object;
    }

    ~mock_instruction_t()
    {
        if (cnt_destroy)
        {
            (*cnt_destroy)++;
        }
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
        if (require_destroy_on_commit.has_value() &&
            cnt_destroy)
        {
            REQUIRE(*cnt_destroy == require_destroy_on_commit);
        }

        ++committed;

        if (ready_on_commit)
        {
            send_ready();
        }
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

inline void setup_txn_timeout(int timeout)
{
    auto section = std::make_shared<wf::config::section_t>("core");
    auto val     = std::make_shared<wf::config::option_t<int>>(
        "transaction_timeout", timeout);
    section->register_new_option(val);
    mock_core().config.merge_section(section);
}
