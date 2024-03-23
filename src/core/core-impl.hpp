#ifndef WF_CORE_CORE_IMPL_HPP
#define WF_CORE_CORE_IMPL_HPP

#include "core/plugin-loader.hpp"
#include "wayfire/core.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/util.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>

namespace wf
{
class seat_t;
class input_manager_t;
class input_method_relay;
class compositor_core_impl_t : public compositor_core_t
{
  public:
    wlr_egl *egl;
    wlr_compositor *compositor;

    std::unique_ptr<wf::input_manager_t> input;
    std::unique_ptr<input_method_relay> im_relay;
    std::unique_ptr<plugin_manager_t> plugin_mgr;

    /**
     * Initialize the compositor core.
     * Called only by main().
     */
    virtual void init();

    /**
     * Finish initialization of core after the backend has started.
     * Called only by main().
     */
    virtual void post_init();

    void fini();

    static compositor_core_impl_t& get();
    static compositor_core_impl_t& allocate_core();
    static void deallocate_core();

    wlr_seat *get_current_seat() override;
    void warp_cursor(wf::pointf_t pos) override;
    void transfer_grab(wf::scene::node_ptr node) override;
    void set_cursor(std::string name) override;
    void unhide_cursor() override;
    void hide_cursor() override;

    wf::pointf_t get_cursor_position() override;
    wf::pointf_t get_touch_position(int id) override;
    const wf::touch::gesture_state_t& get_touch_state() override;

    wf::scene::node_ptr get_cursor_focus() override;
    wf::scene::node_ptr get_touch_focus() override;

    void add_touch_gesture(
        nonstd::observer_ptr<wf::touch::gesture_t> gesture) override;
    void rem_touch_gesture(
        nonstd::observer_ptr<wf::touch::gesture_t> gesture) override;

    std::vector<nonstd::observer_ptr<wf::input_device_t>> get_input_devices()
    override;
    virtual wlr_cursor *get_wlr_cursor() override;

    std::string get_xwayland_display() override;
    pid_t run(std::string command) override;
    void shutdown() override;
    compositor_state_t get_current_state() override;
    const std::shared_ptr<scene::root_node_t>& scene() final;

    compositor_core_impl_t();
    virtual ~compositor_core_impl_t();

  protected:
    wf::wl_listener_wrapper vkbd_created;
    wf::wl_listener_wrapper vptr_created;
    wf::wl_listener_wrapper input_inhibit_activated;
    wf::wl_listener_wrapper input_inhibit_deactivated;
    wf::wl_listener_wrapper pointer_constraint_added;
    wf::wl_listener_wrapper idle_inhibitor_created;
    wf::wl_listener_wrapper drm_lease_request;
    std::shared_ptr<scene::root_node_t> scene_root;

    compositor_state_t state = compositor_state_t::UNKNOWN;

  private:
    wf::option_wrapper_t<bool> discard_command_output;
    static std::unique_ptr<compositor_core_impl_t> static_core;
};

compositor_core_impl_t& get_core_impl();

void priv_output_layout_fini(wf::output_layout_t *layout);
}

#endif /* end of include guard: WF_CORE_CORE_IMPL_HPP */
