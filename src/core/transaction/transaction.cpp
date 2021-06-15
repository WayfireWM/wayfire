#include <wayfire/debug.hpp>

#include "transaction-priv.hpp"
#include "../core-impl.hpp"

namespace wf
{
namespace txn
{
void transaction_impl_t::set_pending()
{
    for (auto& i : this->instructions)
    {
        i->set_pending();
    }
}

void transaction_impl_t::commit()
{
    for (auto& i : this->instructions)
    {
        i->commit();
    }
}

void transaction_impl_t::apply()
{
    for (auto& i : this->instructions)
    {
        i->apply();
    }
}

void transaction_impl_t::merge(transaction_iuptr_t other)
{
    std::move(other->instructions.begin(), other->instructions.end(),
        std::back_inserter(instructions));
}

bool transaction_impl_t::does_intersect(const transaction_impl_t& other) const
{
    auto objs = get_objects();
    auto other_objs = other.get_objects();

    std::vector<std::string> intersection;

    std::set_intersection(objs.begin(), objs.end(),
        other_objs.begin(), other_objs.end(), std::back_inserter(intersection));

    return !intersection.empty();
}

void transaction_impl_t::add_instruction(instruction_uptr_t instr)
{
    this->instructions.push_back(std::move(instr));
}

std::set<std::string> transaction_impl_t::get_objects() const
{
    std::set<std::string> objs;
    for (auto& i : instructions)
    {
        objs.insert(i->get_object());
    }

    return objs;
}

std::set<wayfire_view> transaction_impl_t::get_views() const
{
    std::set<wayfire_view> views;
    for (auto& i : instructions)
    {
        auto view = wf::get_core_impl().find_view(i->get_object());
        if (view)
        {
            views.insert(view);
        }
    }

    return views;
}

transaction_uptr_t transaction_t::create()
{
    return std::make_unique<transaction_impl_t>();
}
}
}
