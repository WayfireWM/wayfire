#include "wayfire/object.hpp"
#include "wayfire/nonstd/safe-list.hpp"
#include <unordered_map>
#include <set>

#include <wayfire/signal-provider.hpp>

void wf::signal::connection_base_t::disconnect()
{
    auto connected_copy = this->connected_to;
    for (auto& x : connected_copy)
    {
        x->disconnect(this);
    }
}

class wf::object_base_t::obase_impl
{
  public:
    std::unordered_map<std::string, std::unique_ptr<custom_data_t>> data;
    uint32_t object_id;
};

wf::object_base_t::object_base_t()
{
    this->obase_priv = std::make_unique<obase_impl>();

    static uint32_t global_id = 0;
    obase_priv->object_id = global_id++;
}

wf::object_base_t::~object_base_t() = default;

std::string wf::object_base_t::to_string() const
{
    return std::to_string(get_id());
}

uint32_t wf::object_base_t::get_id() const
{
    return obase_priv->object_id;
}

bool wf::object_base_t::has_data(std::string name)
{
    return obase_priv->data[name] != nullptr;
}

void wf::object_base_t::erase_data(std::string name)
{
    auto data = std::move(obase_priv->data[name]);
    obase_priv->data.erase(name);
    data.reset();
}

wf::custom_data_t*wf::object_base_t::_fetch_data(std::string name)
{
    auto it = obase_priv->data.find(name);
    if (it == obase_priv->data.end())
    {
        return nullptr;
    }

    return it->second.get();
}

wf::custom_data_t*wf::object_base_t::_fetch_erase(std::string name)
{
    auto data = obase_priv->data[name].release();
    erase_data(name);

    return data;
}

void wf::object_base_t::_store_data(std::unique_ptr<wf::custom_data_t> data,
    std::string name)
{
    obase_priv->data[name] = std::move(data);
}

void wf::object_base_t::_clear_data()
{
    obase_priv->data.clear();
}
