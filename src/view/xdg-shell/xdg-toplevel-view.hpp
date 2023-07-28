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
    xdg_toplevel_view_t(wlr_xdg_toplevel *tlvl);

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
    void initialize() override;

    void set_decoration_mode(bool use_csd);

  private:
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

    std::shared_ptr<wf::xdg_toplevel_t> wtoplevel;
    wf::signal::connection_t<xdg_toplevel_applied_state_signal> on_toplevel_applied;
    wf::geometry_t last_bounding_box = {0, 0, 0, 0};

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
