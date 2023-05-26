#ifndef XDG_SHELL_HPP
#define XDG_SHELL_HPP

#include "view-impl.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"

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
    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_keyboard_focus_changed;

    wlr_xdg_popup *popup;
    void unconstrain();
    void update_position();

  public:
    wayfire_xdg_popup(wlr_xdg_popup *popup);
    void initialize() override;

    wayfire_view popup_parent;
    virtual void map(wlr_surface *surface) override;
    virtual void commit() override;
    virtual void destroy() override;
    virtual void close() override;
    void ping() final;
};

void create_xdg_popup(wlr_xdg_popup *popup);

#endif /* end of include guard: XDG_SHELL_HPP */
