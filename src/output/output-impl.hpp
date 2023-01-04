#include "wayfire/output.hpp"
#include "../core/seat/bindings-repository.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"

#include <memory>
#include <unordered_set>
#include <wayfire/nonstd/safe-list.hpp>
#include <wayfire/workspace-manager.hpp>

namespace wf
{
class output_impl_t : public output_t
{
  private:
    std::shared_ptr<scene::output_node_t> nodes[TOTAL_LAYERS];
    scene::floating_inner_ptr wset;
    uint64_t last_timestamp = 0;

  private:
    std::unordered_multiset<wf::plugin_grab_interface_t*> active_plugins;
    std::unique_ptr<wf::bindings_repository_t> bindings;

    signal_connection_t view_disappeared_cb;
    bool inhibited = false;

    enum focus_view_flags_t
    {
        /* Raise the view which is being focused */
        FOCUS_VIEW_RAISE        = (1 << 0),
        /* Close popups of non-focused views */
        FOCUS_VIEW_CLOSE_POPUPS = (1 << 1),
    };

    /**
     * Set the given view as the active view.
     */
    void update_active_view(wayfire_view view);

    /**
     * Close all popups on the output which do not belong to the active view.
     */
    void close_popups();

    /** @param flags bitmask of @focus_view_flags_t */
    void focus_view(wayfire_view view, uint32_t flags);

    wf::dimensions_t effective_size;

    void do_update_focus(wf::scene::node_t *node);

  public:
    output_impl_t(wlr_output *output, const wf::dimensions_t& effective_size);

    virtual ~output_impl_t();
    wayfire_view active_view = nullptr;
    wayfire_view last_active_toplevel = nullptr;

    /**
     * Implementations of the public APIs
     */
    std::shared_ptr<wf::scene::output_node_t> node_for_layer(
        wf::scene::layer layer) const override;
    scene::floating_inner_ptr get_wset() const override;
    bool can_activate_plugin(wf::plugin_grab_interface_t *owner, uint32_t flags = 0) override;
    bool can_activate_plugin(uint32_t caps, uint32_t flags = 0) override;
    bool activate_plugin(wf::plugin_grab_interface_t *owner, uint32_t flags = 0) override;
    bool deactivate_plugin(wf::plugin_grab_interface_t *owner) override;
    void cancel_active_plugins() override;
    bool is_plugin_active(std::string owner_name) const override;
    bool call_plugin(const std::string& activator,
        const wf::activator_data_t& data) const override;
    void focus_view(wayfire_view v, bool raise) override;
    wf::dimensions_t get_screen_size() const override;

    wf::binding_t *add_key(option_sptr_t<keybinding_t> key,
        wf::key_callback*) override;
    wf::binding_t *add_axis(option_sptr_t<keybinding_t> axis,
        wf::axis_callback*) override;
    wf::binding_t *add_button(option_sptr_t<buttonbinding_t> button,
        wf::button_callback*) override;
    wf::binding_t *add_activator(option_sptr_t<activatorbinding_t> activator,
        wf::activator_callback*) override;
    void rem_binding(wf::binding_t *binding) override;
    void rem_binding(void *callback) override;

    wayfire_view get_active_view() const override;
    void focus_node(wf::scene::node_ptr new_focus) override;
    void refocus() override;
    uint64_t get_last_focus_timestamp() const override;

    /**
     * Set the output as inhibited, so that no plugins can be activated
     * except those that ignore inhibitions.
     */
    void inhibit_plugins();

    /**
     * Uninhibit the output.
     */
    void uninhibit_plugins();

    /** @return true if the output is inhibited */
    bool is_inhibited() const;

    /** @return The bindings repository of the output */
    bindings_repository_t& get_bindings();

    /** Set the effective resolution of the output */
    void set_effective_size(const wf::dimensions_t& size);
};

/**
 * Set the last focused timestamp of the view to now.
 */
void update_focus_timestamp(wayfire_view view);
}
