#pragma once

#include "wayfire/signal-provider.hpp"
#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>

namespace wf
{
/**
 * A base class for plugins which want to have an instance per output.
 */
class per_output_plugin_instance_t
{
  public:
    // Should be set before initializing the plugin instance. Usually done by per_output_tracker_mixin_t.
    wf::output_t *output = nullptr;

    virtual void init()
    {}
    virtual void fini()
    {}
    virtual ~per_output_plugin_instance_t() = default;
};

/**
 * A mixin class which can be used to setup per-output-instance tracking.
 */
template<class ConcretePluginType = per_output_plugin_instance_t>
class per_output_tracker_mixin_t
{
  public:
    virtual ~per_output_tracker_mixin_t() = default;

    void init_output_tracking()
    {
        auto& ol = wf::get_core().output_layout;
        ol->connect(&on_output_added);
        ol->connect(&on_output_removed);

        for (auto wo : ol->get_outputs())
        {
            handle_new_output(wo);
        }
    }

    void fini_output_tracking()
    {
        on_output_added.disconnect();
        on_output_removed.disconnect();

        for (auto& [output, inst] : output_instance)
        {
            inst->fini();
        }

        output_instance.clear();
    }

  protected:
    std::map<wf::output_t*, std::unique_ptr<ConcretePluginType>> output_instance;
    wf::signal::connection_t<output_added_signal> on_output_added = [=] (output_added_signal *ev)
    {
        handle_new_output(ev->output);
    };

    wf::signal::connection_t<output_pre_remove_signal> on_output_removed = [=] (output_pre_remove_signal *ev)
    {
        handle_output_removed(ev->output);
    };

    virtual void handle_new_output(wf::output_t *output)
    {
        auto inst = std::make_unique<ConcretePluginType>();
        inst->output = output;
        auto ptr = inst.get();
        output_instance[output] = std::move(inst);
        ptr->init();
    }

    virtual void handle_output_removed(wf::output_t *output)
    {
        output_instance[output]->fini();
        output_instance.erase(output);
    }
};

template<class ConcretePluginType>
class per_output_plugin_t : public wf::plugin_interface_t,
    public per_output_tracker_mixin_t<ConcretePluginType>
{
  public:
    void init() override
    {
        this->init_output_tracking();
    }

    void fini() override
    {
        this->fini_output_tracking();
    }
};
}
