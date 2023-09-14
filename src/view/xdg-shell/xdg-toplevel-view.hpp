#include "view/toplevel-node.hpp"
#include "wayfire/geometry.hpp"
#include <wayfire/view.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include "wayfire/unstable/wlr-surface-node.hpp"
#include "../surface-impl.hpp"

#include <wayfire/toplevel-view.hpp>
#include "wayfire/toplevel.hpp"
#include "xdg-toplevel.hpp"

namespace wf
{
/**
 * An implementation of view_interface_t for xdg-shell toplevels.
 */
class xdg_toplevel_view_t : public wf::toplevel_view_interface_t
{
  public:
    static std::shared_ptr<xdg_toplevel_view_t> create(wlr_xdg_toplevel *toplevel);

    void request_native_size() override;
    void close() override;
    void ping() override;
    wlr_surface *get_keyboard_focus_surface() override;
    bool is_focusable() const override;
    void set_activated(bool active) override;
    std::string get_app_id() override;
    std::string get_title() override;
    bool should_be_decorated() override;
    bool is_mapped() const override;
    void set_decoration_mode(bool use_csd);

  private:
    friend class wf::tracking_allocator_t<view_interface_t>;
    xdg_toplevel_view_t(wlr_xdg_toplevel *tlvl);
    bool has_client_decoration = true;
    bool _is_mapped = false;
    wf::wl_listener_wrapper on_map, on_unmap, on_destroy, on_new_popup,
        on_request_move, on_request_resize,
        on_request_minimize, on_request_maximize,
        on_request_fullscreen, on_set_parent,
        on_set_title, on_set_app_id, on_show_window_menu,
        on_ping_timeout;

    std::string app_id;
    std::string title;

    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;
    std::shared_ptr<wf::toplevel_view_node_t> surface_root_node;

    // A reference to 'this' used while unmapping, to ensure that the view lives until unmap happens.
    std::shared_ptr<wf::view_interface_t> _self_ref;

    std::shared_ptr<wf::xdg_toplevel_t> wtoplevel;
    wf::signal::connection_t<xdg_toplevel_applied_state_signal> on_toplevel_applied;
    wlr_xdg_toplevel *xdg_toplevel;

    void map();
    void unmap();
    void destroy();

    void handle_title_changed(std::string new_title);
    void handle_app_id_changed(std::string new_app_id);
    void handle_toplevel_state_changed(toplevel_state_t old_state);
};

void default_handle_new_xdg_toplevel(wlr_xdg_toplevel *toplevel);
}
