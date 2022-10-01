#ifndef VIEW_IMPL_HPP
#define VIEW_IMPL_HPP

#include <wayfire/nonstd/safe-list.hpp>
#include <wayfire/view.hpp>
#include <wayfire/opengl.hpp>

#include "surface-impl.hpp"
#include "wayfire/scene.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>

// for emit_map_*()
#include <wayfire/compositor-view.hpp>
#include <wayfire/compositor-surface.hpp>

struct wlr_seat;
namespace wf
{
struct view_transform_block_t
{
    std::string plugin_name = "";
    std::unique_ptr<wf::view_transformer_t> transform;
    wf::render_target_t fb;

    view_transform_block_t();
    ~view_transform_block_t();

    view_transform_block_t(const view_transform_block_t &) = delete;
    view_transform_block_t(view_transform_block_t &&) = delete;
    view_transform_block_t& operator =(const view_transform_block_t&) = delete;
    view_transform_block_t& operator =(view_transform_block_t&&) = delete;
};

/** Private data used by the default view_interface_t implementation */
class view_interface_t::view_priv_impl
{
  public:
    /**
     * A view is alive as long as it is possible for it to become mapped in the
     * future. For wlr views, this means that their role object hasn't been
     * destroyed and they still have the internal surface reference.
     */
    bool is_alive = true;
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

    uint32_t edges = 0;
    int in_continuous_move   = 0;
    int in_continuous_resize = 0;
    int visibility_counter   = 1;

    wf::safe_list_t<std::shared_ptr<view_transform_block_t>> transforms;

    struct offscreen_buffer_t : public wf::render_target_t
    {
        wf::region_t cached_damage;
        bool valid()
        {
            return this->fb != (uint32_t)-1;
        }
    } offscreen_buffer;

    wlr_box minimize_hint = {0, 0, 0, 0};

    scene::floating_inner_ptr root_node;
    scene::floating_inner_ptr transformed_node;
    std::shared_ptr<scene::view_node_t> surface_root_node;
    bool actually_minimized = false;

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
class wlr_view_t :
    public wlr_surface_base_t,
    public view_interface_t
{
  public:
    wlr_view_t();
    virtual ~wlr_view_t() = default;
    wlr_view_t(const wlr_view_t &) = delete;
    wlr_view_t(wlr_view_t &&) = delete;
    wlr_view_t& operator =(const wlr_view_t&) = delete;
    wlr_view_t& operator =(wlr_view_t&&) = delete;

    /* Functions which are shell-independent */
    virtual void set_role(view_role_t new_role) override final;

    virtual std::string get_app_id() override final;
    virtual std::string get_title() override final;

    /* Functions which are further specialized for the different shells */
    virtual void move(int x, int y) override;
    virtual wf::geometry_t get_wm_geometry() override;
    virtual wf::geometry_t get_output_geometry() override;

    virtual wlr_surface *get_keyboard_focus_surface() override;

    virtual bool should_be_decorated() override;
    virtual void set_decoration_mode(bool use_csd);
    virtual void set_output(wf::output_t*) override;
    bool has_client_decoration = true;

  protected:
    std::string title, app_id;
    /** Used by view implementations when the app id changes */
    void handle_app_id_changed(std::string new_app_id);
    /** Used by view implementations when the title changes */
    void handle_title_changed(std::string new_title);
    /* Update the minimize hint */
    void handle_minimize_hint(wf::surface_interface_t *relative_to,
        const wlr_box& hint);

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

    virtual void commit() override;
    virtual void map(wlr_surface *surface) override;
    virtual void unmap() override;

    /* Handle the destruction of the underlying wlroots object */
    virtual void destroy();

    /*
     * wlr_foreign_toplevel_v1 implementation functions
     */

    /* The toplevel is created by the individual view's mapping functions,
     * i.e in xdg-shell, xwayland, etc.
     * The handle is automatically destroyed when the view is unmapped */
    wlr_foreign_toplevel_handle_v1 *toplevel_handle = NULL;

    wf::wl_listener_wrapper toplevel_handle_v1_maximize_request,
        toplevel_handle_v1_activate_request,
        toplevel_handle_v1_minimize_request,
        toplevel_handle_v1_set_rectangle_request,
        toplevel_handle_v1_close_request;

    /* Create/destroy the toplevel_handle */
    virtual void create_toplevel();
    virtual void destroy_toplevel();

    /* The following are no-op if toplevel_handle == NULL */
    virtual void toplevel_send_title();
    virtual void toplevel_send_app_id();
    virtual void toplevel_send_state();
    virtual void toplevel_update_output(wf::output_t *output, bool enter);

    virtual void desktop_state_updated() override;

  public:
    /* Just pass to the default wlr surface implementation */
    virtual bool is_mapped() const override
    {
        return _is_mapped();
    }

    virtual wf::dimensions_t get_size() const override
    {
        return _get_size();
    }

    virtual void simple_render(const wf::render_target_t& fb, int x, int y,
        const wf::region_t& damage) override
    {
        _simple_render(fb, x, y, damage);
    }
};

/** Emit the map signal for the given view */
void emit_view_map_signal(wayfire_view view, bool has_position);
void emit_ping_timeout_signal(wayfire_view view);

wf::surface_interface_t *wf_surface_from_void(void *handle);
wf::view_interface_t *wf_view_from_void(void *handle);

void init_xdg_shell();
void init_xwayland();
void init_layer_shell();

std::string xwayland_get_display();
void xwayland_update_default_cursor();

/* Ensure that the given surface is on top of the Xwayland stack order. */
void xwayland_bring_to_front(wlr_surface *surface);

void init_desktop_apis();
}

#endif /* end of include guard: VIEW_IMPL_HPP */
