#ifndef XDG_SHELL_HPP
#define XDG_SHELL_HPP

#include "view-impl.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/seat.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/view.hpp"

class wayfire_xdg_popup_node;

/**
 * A class for xdg-shell popups
 */
class wayfire_xdg_popup : public wf::view_interface_t
{
  protected:
    wf::wl_listener_wrapper on_new_popup, on_map, on_unmap, on_ping_timeout, on_reposition;

    wf::signal::connection_t<wf::view_geometry_changed_signal> parent_geometry_changed;
    wf::signal::connection_t<wf::view_title_changed_signal> parent_title_changed;
    wf::signal::connection_t<wf::view_app_id_changed_signal> parent_app_id_changed;
    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_keyboard_focus_changed;

    void unconstrain();
    void update_position();
    wayfire_xdg_popup(wlr_xdg_popup *popup);
    friend class wf::tracking_allocator_t<view_interface_t>;

  public:
    ~wayfire_xdg_popup();
    static std::shared_ptr<wayfire_xdg_popup> create(wlr_xdg_popup *popup);

    wayfire_view popup_parent;
    wlr_xdg_popup *popup;
    void map();
    void unmap();
    void commit();
    void destroy();
    void close() override;
    void ping() override final;

    /* Just pass to the default wlr surface implementation */
    bool is_mapped() const override;

    std::string get_app_id() override final;
    std::string get_title() override final;

    bool is_focusable() const override final;

    void move(int x, int y);
    wf::geometry_t get_geometry();
    virtual wlr_surface *get_keyboard_focus_surface() override;

  private:
    /**
     * The bounding box of the view the last time it was rendered.
     *
     * This is used to damage the view when it is resized, because when a
     * transformer changes because the view is resized, we can't reliably
     * calculate the old view region to damage.
     */
    wf::geometry_t last_bounding_box{0, 0, 0, 0};

    /** The output geometry of the view */
    wf::geometry_t geometry{100, 100, 0, 0};

    wf::wl_listener_wrapper on_surface_commit;
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;
    std::shared_ptr<wayfire_xdg_popup_node> surface_root_node;

    std::string title, app_id;
    void handle_app_id_changed(std::string new_app_id);
    void handle_title_changed(std::string new_title);
    void update_size();

    bool should_close_on_focus_change(wf::keyboard_focus_changed_signal *ev);
};

void create_xdg_popup(wlr_xdg_popup *popup);

#endif /* end of include guard: XDG_SHELL_HPP */
