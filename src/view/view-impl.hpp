#ifndef VIEW_IMPL_HPP
#define VIEW_IMPL_HPP

#include <memory>
#include <wayfire/nonstd/safe-list.hpp>
#include <wayfire/view.hpp>
#include <wayfire/opengl.hpp>

#include "surface-impl.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"
#include "wayfire/output.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/util.hpp"
#include "wayfire/view-transform.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/compositor-view.hpp>

struct wlr_seat;
namespace wf
{
/** Private data used by the default view_interface_t implementation */
class view_interface_t::view_priv_impl
{
  public:
    wlr_surface *wsurface = nullptr;

    /** Reference count to the view */
    int ref_cnt = 0;

    size_t last_view_cnt = 0;

    bool keyboard_focus_enabled = true;

    /**
     * Calculate the windowed geometry relative to the output's workarea.
     */
    wf::geometry_t calculate_windowed_geometry(wf::output_t *output);

    /**
     * Update the stored window geometry and workarea, if the current view
     * state is not-tiled and not-moving.
     */
    void update_windowed_geometry(wayfire_view self, wf::geometry_t geometry);

    std::unique_ptr<wf::decorator_frame_t_t> frame = nullptr;

    uint32_t allowed_actions = VIEW_ALLOW_ALL;

    uint32_t edges = 0;
    int in_continuous_move   = 0;
    int in_continuous_resize = 0;
    int visibility_counter   = 1;

    wf::render_target_t offscreen_buffer;
    wlr_box minimize_hint = {0, 0, 0, 0};

    scene::floating_inner_ptr root_node;
    std::shared_ptr<scene::transform_manager_node_t> transformed_node;
    std::unique_ptr<wlr_surface_controller_t> surface_controller;
    scene::floating_inner_ptr surface_root_node;
    wf::output_t *output;

    void set_mapped(bool mapped);
    void set_mapped_surface_contents(std::shared_ptr<scene::wlr_surface_node_t> content);
    void unset_mapped_surface_contents();

  private:
    /** Last geometry the view has had in non-tiled and non-fullscreen state.
     * -1 as width/height means that no such geometry has been stored. */
    wf::geometry_t last_windowed_geometry = {0, 0, -1, -1};

    /**
     * The workarea when last_windowed_geometry was stored. This is used
     * for ex. when untiling a view to determine its geometry relative to the
     * (potentially changed) workarea of its output.
     */
    wf::geometry_t windowed_geometry_workarea = {0, 0, -1, -1};
};

/**
 * Damage the given box, assuming the damage belongs to the given view.
 * The given box is assumed to have been transformed with the view's
 * transformers.
 *
 * The main difference with directly damaging the output is that this will
 * add the damage to all workspaces the view is visible on, in case of shell
 * views.
 */
void view_damage_raw(wayfire_view view, const wlr_box& box);

/**
 * Implementation of a view backed by a wlr_* shell struct.
 */
class wlr_view_t : public view_interface_t
{
  public:
    wlr_view_t();
    virtual ~wlr_view_t() = default;

    virtual std::string get_app_id() override final;
    virtual std::string get_title() override final;

    /* Functions which are further specialized for the different shells */
    virtual void move(int x, int y) override;
    virtual wf::geometry_t get_wm_geometry() override;
    virtual wf::geometry_t get_output_geometry() override;

    virtual wlr_surface *get_keyboard_focus_surface() override;

    virtual bool should_be_decorated() override;
    virtual void set_decoration_mode(bool use_csd);
    bool has_client_decoration = true;

  protected:
    std::string title, app_id;
    /** Used by view implementations when the app id changes */
    void handle_app_id_changed(std::string new_app_id);
    /** Used by view implementations when the title changes */
    void handle_title_changed(std::string new_title);

    /**
     * The bounding box of the view the last time it was rendered.
     *
     * This is used to damage the view when it is resized, because when a
     * transformer changes because the view is resized, we can't reliably
     * calculate the old view region to damage.
     */
    wf::geometry_t last_bounding_box{0, 0, 0, 0};

    /**
     * Adjust the view position when resizing the view so that its apparent
     * position doesn't change when resizing.
     */
    void adjust_anchored_edge(wf::dimensions_t new_size);

    /** The output geometry of the view */
    wf::geometry_t geometry{100, 100, 0, 0};

    /** Set the view position and optionally send the geometry changed signal
     * @param old_geometry The geometry to report as previous, in case the
     * signal is sent. */
    virtual void set_position(int x, int y, wf::geometry_t old_geometry,
        bool send_geometry_signal);
    /** Update the view size to the actual dimensions of its surface */
    virtual void update_size();

    /** Last request to the client */
    wf::dimensions_t last_size_request = {0, 0};
    virtual bool should_resize_client(wf::dimensions_t request,
        wf::dimensions_t current_size);

    virtual void commit();
    virtual void map(wlr_surface *surface);
    virtual void unmap();

    /* Handle the destruction of the underlying wlroots object */
    virtual void destroy();

    wf::wl_listener_wrapper on_surface_commit;
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;

  public:
    /* Just pass to the default wlr surface implementation */
    bool is_mapped() const override;
    wf::dimensions_t get_size() const;

    wlr_buffer *get_buffer();
};

/** Emit the map signal for the given view */
void emit_view_map_signal(wayfire_view view, bool has_position);
void emit_ping_timeout_signal(wayfire_view view);
void emit_geometry_changed_signal(wayfire_view view, wf::geometry_t old_geometry);

void init_xdg_shell();
void init_xwayland();
void init_layer_shell();

std::string xwayland_get_display();
void xwayland_update_default_cursor();

/* Ensure that the given surface is on top of the Xwayland stack order. */
void xwayland_bring_to_front(wlr_surface *surface);

void init_desktop_apis();
void init_xdg_decoration_handlers();
}

#endif /* end of include guard: VIEW_IMPL_HPP */
