#include "wayfire/object.hpp"
#include "wayfire/nonstd/safe-list.hpp"
#include <unordered_map>
#include <set>

/* Implementation note: because of circular dependencies between
 * signal_connection_t and signal_provider_t, the chosen way to resolve
 * them is to have signal_provider_t directly modify signal_connection_t
 * private data when needed. */

class wf::signal_connection_t::impl
{
  public:
    signal_callback_t callback;
    std::set<nonstd::observer_ptr<signal_provider_t>> connected_providers;

    void add(signal_provider_t *provider)
    {
        connected_providers.insert(provider);
    }

    void remove(signal_provider_t *provider)
    {
        connected_providers.erase(provider);
    }
};

wf::signal_connection_t::signal_connection_t()
{
    this->priv = std::make_unique<impl>();
}

wf::signal_connection_t::~signal_connection_t()
{
    disconnect();
}

void wf::signal_connection_t::set_callback(signal_callback_t callback)
{
    priv->callback = callback;
}

void wf::signal_connection_t::emit(signal_data_t *data)
{
    if (this->priv->callback)
    {
        this->priv->callback(data);
    }
}

void wf::signal_connection_t::disconnect()
{
    auto connected = this->priv->connected_providers;
    for (auto& provider : connected)
    {
        provider->disconnect_signal(this);
    }
}

class wf::signal_provider_t::sprovider_impl
{
  public:
    std::unordered_map<std::string,
        wf::safe_list_t<signal_connection_t*>> signals;
};

wf::signal_provider_t::signal_provider_t()
{
    this->sprovider_priv = std::make_unique<sprovider_impl>();
}

wf::signal_provider_t::~signal_provider_t()
{
    for (auto& s : sprovider_priv->signals)
    {
        s.second.for_each([=] (signal_connection_t *connection)
        {
            connection->priv->remove(this);
        });
    }
}

void wf::signal_provider_t::connect_signal(std::string name,
    signal_connection_t *callback)
{
    const auto it = sprovider_priv->signals.try_emplace(name).first;
    it->second.push_back(callback);
    callback->priv->add(this);
}

void wf::signal_provider_t::disconnect_signal(signal_connection_t *connection)
{
    for (auto& s : sprovider_priv->signals)
    {
        s.second.remove_if([=] (signal_connection_t *connected)
        {
            if (connected == connection)
            {
                connected->priv->remove(this);

                return true;
            }

            return false;
        });
    }
}

/* Emit the given signal. No type checking for data is required */
void wf::signal_provider_t::emit_signal(std::string name, wf::signal_data_t *data)
{
    const auto it = sprovider_priv->signals.find(name);
    if (it != sprovider_priv->signals.end())
    {
        it->second.for_each([data] (auto call)
        {
            call->emit(data);
        });
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
    return obase_priv->data.count(name) != 0;
}

void wf::object_base_t::erase_data(std::string name)
{
    const auto it = obase_priv->data.find(name);
    if (it != obase_priv->data.end())
    {
        it->second.reset();
        obase_priv->data.erase(it);
    }
}

wf::custom_data_t*wf::object_base_t::_fetch_data(std::string name)
{
    const auto it = obase_priv->data.find(name);
    if (it == obase_priv->data.end())
    {
        return nullptr;
    }

    return it->second.get();
}

wf::custom_data_t*wf::object_base_t::_fetch_erase(std::string name)
{
    const auto it = obase_priv->data.find(name);
    if (it != obase_priv->data.end())
    {
        const auto data = it->second.release();
        obase_priv->data.erase(it);

        return data;
    } else
    {
        return nullptr;
    }
}

void wf::object_base_t::_store_data(std::unique_ptr<wf::custom_data_t> data,
    std::string name)
{
    (void)obase_priv->data.insert_or_assign(name, std::move(data));
}

void wf::object_base_t::_clear_data()
{
    obase_priv->data.clear();
}
