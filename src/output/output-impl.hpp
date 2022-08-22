#include "wayfire/output.hpp"
#include "plugin-loader.hpp"
#include "../core/seat/bindings-repository.hpp"

#include <unordered_set>
#include <wayfire/nonstd/safe-list.hpp>
#include <wayfire/workspace-manager.hpp>

namespace wf
{
class output_impl_t : public output_t
{
  private:
    std::shared_ptr<scene::output_node_t> nodes[TOTAL_LAYERS];

  private:
    std::unordered_multiset<wf::plugin_grab_interface_t*> active_plugins;
    std::unique_ptr<plugin_manager> plugin;
    std::unique_ptr<wf::bindings_repository_t> bindings;

    signal_connection_t view_disappeared_cb;
    bool inhibited = false;

    enum focus_view_flags_t
    {
        /* Raise the view which is being focused */
        FOCUS_VIEW_RAISE        = (1 << 0),
        /* Close popups of non-focused views */
        FOCUS_VIEW_CLOSE_POPUPS = (1 << 1),
        /* Inhibit updating the focus timestamp of the view */
        FOCUS_VIEW_NOBUMP       = (1 << 2),
    };

    /**
     * Set the given view as the active view.
     * If the output has focus, try to focus the view as well.
     *
     * @param flags bitmask of @focus_view_flags_t
     */
    void update_active_view(wayfire_view view, uint32_t flags);

    /**
     * Close all popups on the output which do not belong to the active view.
     */
    void close_popups();

    /** @param flags bitmask of @focus_view_flags_t */
    void focus_view(wayfire_view view, uint32_t flags);

    wf::dimensions_t effective_size;

  public:
    output_impl_t(wlr_output *output, const wf::dimensions_t& effective_size);
    /**
     * Start all the plugins on this output.
     */
    void start_plugins();

    virtual ~output_impl_t();
    wayfire_view active_view;

    /**
     * Implementations of the public APIs
     */
    virtual std::shared_ptr<wf::scene::output_node_t> node_for_layer(
        wf::scene::layer layer) const override;
    bool can_activate_plugin(const plugin_grab_interface_uptr& owner,
        uint32_t flags = 0) override;
    bool can_activate_plugin(uint32_t caps, uint32_t flags = 0) override;
    bool activate_plugin(const plugin_grab_interface_uptr& owner,
        uint32_t flags = 0) override;
    bool deactivate_plugin(const plugin_grab_interface_uptr& owner) override;
    void cancel_active_plugins() override;
    bool is_plugin_active(std::string owner_name) const override;
    bool call_plugin(const std::string& activator,
        const wf::activator_data_t& data) const override;
    wayfire_view get_active_view() const override;
    void focus_view(wayfire_view v, bool raise) override;
    void refocus(wayfire_view skip_view, uint32_t layers) override;
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

    /**
     * @return The currently active input grab interface, or nullptr if none
     */
    plugin_grab_interface_t *get_input_grab_interface();

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
