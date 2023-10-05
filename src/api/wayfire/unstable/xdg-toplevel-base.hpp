#pragma once

#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/view.hpp>
#include <wayfire/unstable/wlr-surface-node.hpp>

namespace wf
{
/**
 * A base class for xdg_toplevel-based views which implements the view_interface_t (but not toplevel_view_t,
 * see @xdg_toplevel_view_t for the full implementation).
 */
class xdg_toplevel_view_base_t : public virtual wf::view_interface_t
{
  public:
    xdg_toplevel_view_base_t(wlr_xdg_toplevel *toplevel, bool autocommit);
    virtual ~xdg_toplevel_view_base_t();

    void close() override;
    void ping() override;
    wlr_surface *get_keyboard_focus_surface() override;
    bool is_focusable() const override;
    std::string get_app_id() override;
    std::string get_title() override;
    bool is_mapped() const override;

    /** Set the view state to mapped. */
    virtual void map();
    /** Set the view state to unmapped. */
    virtual void unmap();

  protected:
    wlr_xdg_toplevel *xdg_toplevel;
    std::string app_id;
    std::string title;
    virtual void destroy();

    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;

    wf::wl_listener_wrapper on_destroy;
    wf::wl_listener_wrapper on_new_popup;
    wf::wl_listener_wrapper on_set_title;
    wf::wl_listener_wrapper on_set_app_id;
    wf::wl_listener_wrapper on_ping_timeout;

    void handle_title_changed(std::string new_title);
    void handle_app_id_changed(std::string new_app_id);
};
}
