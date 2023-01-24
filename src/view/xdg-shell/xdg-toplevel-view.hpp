#include "wayfire/geometry.hpp"
#include <wayfire/view.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include "../wlr-surface-node.hpp"
#include "../surface-impl.hpp"

namespace wf
{
/**
 * An implementation of view_interface_t for xdg-shell toplevels.
 */
class xdg_toplevel_view_t : public wf::view_interface_t
{
  public:
    xdg_toplevel_view_t(wlr_xdg_toplevel *tlvl);

    void move(int x, int y) override;
    void resize(int w, int h) override;
    void request_native_size() override;
    void close() override;
    void ping() override;
    wf::geometry_t get_wm_geometry() override;
    wf::geometry_t get_output_geometry() override;
    wlr_surface *get_keyboard_focus_surface() override;
    bool is_focusable() const override;
    void set_tiled(uint32_t edges) override;
    void set_fullscreen(bool fullscreen) override;
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

    wf::wl_listener_wrapper on_surface_commit;
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;
    std::unique_ptr<wlr_surface_controller_t> surface_controller;

    wf::geometry_t wm_geometry = {100, 100, 0, 0};
    wf::geometry_t base_geometry = {100, 100, 0, 0};
    wf::point_t wm_offset;

    wf::dimensions_t last_size_request = {0, 0};
    wlr_xdg_toplevel *xdg_toplevel;
    uint32_t last_configure_serial;
    bool should_resize_client(wf::dimensions_t old, wf::dimensions_t next);
    void update_size();
    void adjust_anchored_edge(wf::dimensions_t new_size);

    void map();
    void unmap();
    void commit();
    void destroy();

    void handle_title_changed(std::string new_title);
    void handle_app_id_changed(std::string new_app_id);
};
}
