#pragma once

// WF_USE_CONFIG_H is set only when building Wayfire itself, external plugins
// need to use <wayfire/config.h>
#ifdef WF_USE_CONFIG_H
    #include <config.h>
#else
    #include <wayfire/config.h>
#endif

#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/view.hpp>
#include <wayfire/unstable/wlr-surface-node.hpp>

namespace wf
{
#if WF_HAS_XWAYLAND
/**
 * A base class for views which base on a wlr_xwayland surface.
 * Contains the implementation of view_interface_t functions used in them.
 */
class xwayland_view_base_t : public virtual wf::view_interface_t
{
  public:
    xwayland_view_base_t(wlr_xwayland_surface *xww);
    virtual ~xwayland_view_base_t();

    virtual void do_map(wlr_surface *surface, bool autocommit, bool emit_map = true);
    virtual void do_unmap();

    virtual void destroy();
    void ping() override;
    void close() override;
    bool is_mapped() const override;
    std::string get_app_id() override;
    std::string get_title() override;

    wlr_surface *get_keyboard_focus_surface() override;
    bool is_focusable() const override;

  protected:
    std::string title, app_id;
    wlr_xwayland_surface *xw;
    bool kb_focus_enabled = true;

    /** Used by view implementations when the app id changes */
    void handle_app_id_changed(std::string new_app_id);

    /** Used by view implementations when the title changes */
    void handle_title_changed(std::string new_title);

    wf::wl_listener_wrapper on_destroy, on_set_title, on_set_app_id, on_ping_timeout;
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;
};
#endif
}
