#include "bindings-repository.hpp"
#include <wayfire/core.hpp>
#include <algorithm>

bool wf::bindings_repository_t::handle_key(const wf::keybinding_t& pressed,
    uint32_t mod_binding_key)
{
    std::vector<std::function<bool()>> callbacks;
    for (auto& binding : this->keys)
    {
        if (binding->activated_by->get_value() == pressed)
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->callback;
            callbacks.emplace_back([pressed, callback] ()
            {
                return (*callback)(pressed);
            });
        }
    }

    for (auto& binding : this->activators)
    {
        if (binding->activated_by->get_value().has_match(pressed))
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->callback;
            callbacks.emplace_back([pressed, callback, mod_binding_key] ()
            {
                wf::activator_data_t ev = {
                    .source = activator_source_t::KEYBINDING,
                    .activation_data = pressed.get_key()
                };

                if (mod_binding_key)
                {
                    ev.source = activator_source_t::MODIFIERBINDING;
                    ev.activation_data = mod_binding_key;
                }

                return (*callback)(ev);
            });
        }
    }

    bool handled = false;
    for (auto& cb : callbacks)
    {
        handled |= cb();
    }

    return handled;
}

bool wf::bindings_repository_t::handle_axis(uint32_t modifiers,
    wlr_pointer_axis_event *ev)
{
    std::vector<wf::axis_callback*> callbacks;

    for (auto& binding : this->axes)
    {
        if (binding->activated_by->get_value() == wf::keybinding_t{modifiers, 0})
        {
            callbacks.push_back(binding->callback);
        }
    }

    for (auto call : callbacks)
    {
        (*call)(ev);
    }

    return !callbacks.empty();
}

bool wf::bindings_repository_t::handle_button(const wf::buttonbinding_t& pressed)
{
    std::vector<std::function<bool()>> callbacks;
    for (auto& binding : this->buttons)
    {
        if (binding->activated_by->get_value() == pressed)
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->callback;
            callbacks.emplace_back([=] ()
            {
                return (*callback)(pressed);
            });
        }
    }

    for (auto& binding : this->activators)
    {
        if (binding->activated_by->get_value().has_match(pressed))
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->callback;
            callbacks.emplace_back([=] ()
            {
                wf::activator_data_t data = {
                    .source = activator_source_t::BUTTONBINDING,
                    .activation_data = pressed.get_button(),
                };
                return (*callback)(data);
            });
        }
    }

    bool binding_handled = false;
    for (auto call : callbacks)
    {
        binding_handled |= call();
    }

    return binding_handled;
}

void wf::bindings_repository_t::handle_gesture(const wf::touchgesture_t& gesture)
{
    std::vector<std::function<void()>> callbacks;
    for (auto& binding : this->activators)
    {
        if (binding->activated_by->get_value().has_match(gesture))
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->callback;
            callbacks.emplace_back([=] ()
            {
                wf::activator_data_t data = {
                    .source = activator_source_t::GESTURE,
                    .activation_data = 0
                };
                (*callback)(data);
            });
        }
    }

    for (auto& cb : callbacks)
    {
        cb();
    }
}

bool wf::bindings_repository_t::handle_activator(
    const std::string& activator, const wf::activator_data_t& data)
{
    auto opt = wf::get_core().config.get_option(activator);
    for (auto& act : this->activators)
    {
        if (act->activated_by == opt)
        {
            return (*act->callback)(data);
        }
    }

    return false;
}

void wf::bindings_repository_t::rem_binding(void *callback)
{
    const auto& erase = [callback] (auto& container)
    {
        auto it = std::remove_if(container.begin(), container.end(),
            [callback] (const auto& ptr)
        {
            return ptr->callback == callback;
        });
        container.erase(it, container.end());
    };

    erase(keys);
    erase(buttons);
    erase(axes);
    erase(activators);

    recreate_hotspots();
}

void wf::bindings_repository_t::rem_binding(binding_t *binding)
{
    const auto& erase = [binding] (auto& container)
    {
        auto it = std::remove_if(container.begin(), container.end(),
            [binding] (const auto& ptr)
        {
            return ptr.get() == binding;
        });
        container.erase(it, container.end());
    };

    erase(keys);
    erase(buttons);
    erase(axes);
    erase(activators);

    recreate_hotspots();
}

wf::bindings_repository_t::bindings_repository_t(wf::output_t *output) :
    hotspot_mgr(output)
{
    on_config_reload.set_callback([=] (wf::signal_data_t*)
    {
        recreate_hotspots();
    });

    wf::get_core().connect_signal("reload-config", &on_config_reload);
}

void wf::bindings_repository_t::recreate_hotspots()
{
    this->idle_recreate_hotspots.run_once([=] ()
    {
        hotspot_mgr.update_hotspots(activators);
    });
}
