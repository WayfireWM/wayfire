#ifndef XDG_SHELL_HPP
#define XDG_SHELL_HPP

#include "view-impl.hpp"
#include "wayfire/signal-definitions.hpp"

/**
 * A class for xdg-shell popups
 */
class wayfire_xdg_popup : public wf::wlr_view_t
{
  protected:
    wf::wl_listener_wrapper on_destroy, on_new_popup,
        on_map, on_unmap, on_ping_timeout;

    wf::signal::connection_t<wf::view_geometry_changed_signal> parent_geometry_changed;
    wf::signal::connection_t<wf::view_title_changed_signal> parent_title_changed;
    wf::signal::connection_t<wf::view_app_id_changed_signal> parent_app_id_changed;

    wf::wl_idle_call pending_close;
    wlr_xdg_popup *popup;
    void unconstrain();
    void update_position();

  public:
    wayfire_xdg_popup(wlr_xdg_popup *popup);
    void initialize() override;

    wlr_view_t *popup_parent;
    virtual void map(wlr_surface *surface) override;
    virtual void commit() override;

    virtual wf::point_t get_window_offset() override;
    virtual void destroy() override;
    virtual void close() override;
    void ping() final;
};

void create_xdg_popup(wlr_xdg_popup *popup);

class wayfire_xdg_view : public wf::wlr_view_t
{
  private:
    wf::wl_listener_wrapper on_map, on_unmap, on_destroy, on_new_popup,
        on_request_move, on_request_resize,
        on_request_minimize, on_request_maximize,
        on_request_fullscreen, on_set_parent,
        on_set_title, on_set_app_id, on_show_window_menu,
        on_ping_timeout;

    wf::point_t xdg_surface_offset = {0, 0};
    wlr_xdg_toplevel *xdg_toplevel;
    uint32_t last_configure_serial = 0;

  protected:
    void initialize() override final;

  public:
    wayfire_xdg_view(wlr_xdg_toplevel *toplevel);
    virtual ~wayfire_xdg_view();

    void map(wlr_surface *surface) final;
    void commit() final;

    wf::point_t get_window_offset() final;
    wf::geometry_t get_wm_geometry() final;

    void set_tiled(uint32_t edges) final;
    void set_activated(bool act) final;
    void set_fullscreen(bool full) final;

    void resize(int w, int h) final;
    void request_native_size() override final;

    void destroy() final;
    void close() final;
    void ping() final;
};

#endif /* end of include guard: XDG_SHELL_HPP */
