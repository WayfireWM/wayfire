#pragma once
#include "output/promotion-manager.hpp"
#include "wayfire/bindings.hpp"
#include "wayfire/output.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"

#include <memory>
#include <unordered_set>
#include <wayfire/nonstd/safe-list.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/output-layout.hpp>
#include <map>

namespace wf
{
class output_impl_t : public output_t
{
  private:
    std::shared_ptr<scene::output_node_t> nodes[(size_t)wf::scene::layer::ALL_LAYERS];

    std::shared_ptr<workspace_set_t> current_wset;
    std::unique_ptr<promotion_manager_t> promotion_manager;

    std::map<key_callback*, key_callback> key_map;
    std::map<axis_callback*, axis_callback> axis_map;
    std::map<button_callback*, button_callback> button_map;
    std::map<activator_callback*, activator_callback> activator_map;

  private:
    std::unordered_multiset<wf::plugin_activation_data_t*> active_plugins;
    wf::signal::connection_t<output_configuration_changed_signal> on_configuration_changed;
    void update_node_limits();

    wf::dimensions_t effective_size;

  public:
    output_impl_t(wlr_output *output, const wf::dimensions_t& effective_size);

    virtual ~output_impl_t();

    /**
     * Implementations of the public APIs
     */
    std::shared_ptr<workspace_set_t> wset() override;
    void set_workspace_set(std::shared_ptr<workspace_set_t> wset) override;
    std::shared_ptr<wf::scene::output_node_t> node_for_layer(
        wf::scene::layer layer) const override;
    bool can_activate_plugin(wf::plugin_activation_data_t *owner, uint32_t flags = 0) override;
    bool can_activate_plugin(uint32_t caps, uint32_t flags = 0) override;
    bool activate_plugin(wf::plugin_activation_data_t *owner, uint32_t flags = 0) override;
    bool deactivate_plugin(wf::plugin_activation_data_t *owner) override;
    void cancel_active_plugins() override;
    bool is_plugin_active(std::string owner_name) const override;
    wf::dimensions_t get_screen_size() const override;

    void add_key(option_sptr_t<keybinding_t> key, wf::key_callback*) override;
    void add_axis(option_sptr_t<keybinding_t> axis, wf::axis_callback*) override;
    void add_button(option_sptr_t<buttonbinding_t> button, wf::button_callback*) override;
    void add_activator(option_sptr_t<activatorbinding_t> activator, wf::activator_callback*) override;
    void rem_binding(void *callback) override;

    /** Set the effective resolution of the output */
    void set_effective_size(const wf::dimensions_t& size);
};

/**
 * Set the last focused timestamp of the view to now.
 */
void update_focus_timestamp(wayfire_view view);

void priv_render_manager_clear_instances(wf::render_manager *manager);
void priv_render_manager_start_rendering(wf::render_manager *manager);
}
