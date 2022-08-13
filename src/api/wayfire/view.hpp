#ifndef VIEW_HPP
#define VIEW_HPP

#include <vector>
#include <wayfire/nonstd/observer_ptr.h>

#include "wayfire/surface.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/decorator.hpp"
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/region.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/scene.hpp>

namespace wf
{
class view_interface_t;
class decorator_frame_t_t;
class view_transformer_t;
}

using wayfire_view = nonstd::observer_ptr<wf::view_interface_t>;

namespace wf
{
class output_t;
namespace scene
{
class view_node_t;
}

/* abstraction for desktop-apis, no real need for plugins
 * This is a base class to all "drawables" - desktop views, subsurfaces, popups */
enum view_role_t
{
    /** Regular views which can be moved around. */
    VIEW_ROLE_TOPLEVEL,
    /** Views which position is fixed externally, for ex. Xwayland OR views */
    VIEW_ROLE_UNMANAGED,
    /**
     * Views which are part of the desktop environment, for example panels,
     * background views, etc.
     */
    VIEW_ROLE_DESKTOP_ENVIRONMENT,
};

/**
 * A bitmask consisting of all tiled edges.
 * This corresponds to a maximized state.
 */
constexpr uint32_t TILED_EDGES_ALL =
    WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT;

/**
 * view_interface_t is the base class for all "toplevel windows", i.e surfaces
 * which have no parent.
 */
class view_interface_t : public surface_interface_t
{
  public:
    const scene::floating_inner_ptr& get_scene_node() const;
    const std::shared_ptr<scene::view_node_t>& get_main_node() const;

    /**
     * The toplevel parent of the view, for ex. the main view of a file chooser
     * dialogue.
     */
    wayfire_view parent = nullptr;

    /**
     * A list of the children views.
     */
    std::vector<wayfire_view> children;

    /**
     * Generate a list of all views in the view's tree.
     * This includes the view itself, its @children and so on.
     *
     * @param mapped_only Whether to include only mapped views.
     *
     * @return A list of all views in the view's tree. This includes the view
     *   itself, its @children and so on.
     */
    std::vector<wayfire_view> enumerate_views(bool mapped_only = true);

    /**
     * Set the toplevel parent of the view, and adjust the children's list of
     * the parent.
     */
    void set_toplevel_parent(wayfire_view parent);

    /** The current view role.  */
    view_role_t role = VIEW_ROLE_TOPLEVEL;

    /** Set the view role */
    virtual void set_role(view_role_t new_role);

    /** Get a textual identifier for this view.  */
    std::string to_string() const;

    /** Wrap the view into a nonstd::observer_ptr<> */
    wayfire_view self();

    /**
     * Set the view's output.
     *
     * If the new output is different from the previous, the view will be
     * removed from the layer it was on the old output.
     */
    virtual void set_output(wf::output_t *new_output) override;

    /** Move the view to the given output-local coordinates.  */
    virtual void move(int x, int y) = 0;

    /**
     * Request that the view change its size to the given dimensions. The view
     * is not obliged to assume the given dimensions.
     *
     * Maximized and tiled views typically do obey the resize request.
     */
    virtual void resize(int w, int h);

    /**
     * A convenience function, has the same effect as calling move and resize
     * atomically.
     */
    virtual void set_geometry(wf::geometry_t g);

    /**
     * Start a resizing mode for this view. While a view is resizing, one edge
     * or corner of the view is made immobile (exactly the edge/corner opposite
     * to the edges which are set as resizing)
     *
     * @param resizing whether to enable or disable resizing mode
     * @param edges the edges which are being resized
     */
    virtual void set_resizing(bool resizing, uint32_t edges = 0);

    /**
     * Set the view in moving mode.
     *
     * @param moving whether to enable or disable moving mode
     */
    virtual void set_moving(bool moving);

    /**
     * Request that the view resizes to its native size.
     */
    virtual void request_native_size();

    /** Request that the view closes. */
    virtual void close();

    /**
     * Ping the view's client.
     * If the ping request times out, `ping-timeout` event will be emitted.
     */
    virtual void ping();

    /**
     * The wm geometry of the view is the portion of the view surface that
     * contains the actual contents, for example, without the view shadows, etc.
     *
     * @return The wm geometry of the view.
     */
    virtual wf::geometry_t get_wm_geometry();

    /**
     * @return the geometry of the view. Coordinates are relative to the current
     * workspace of the view's output, or with undefined origin if the view is
     * not on any output. This doesn't take into account the view's transformers.
     */
    virtual wf::geometry_t get_output_geometry() = 0;

    /**
     * @return The bounding box of the view, which includes all (sub)surfaces,
     * menus, etc. after applying the view transformations.
     */
    virtual wlr_box get_bounding_box();

    /**
     * Find the surface in the view's tree which contains the given point.
     *
     * @param cursor The coordinate of the point to search at
     * @param local The coordinate of the point relative to the returned surface
     *
     * @return The surface which is at the given point, or nullptr if no such
     *         surface was found (in which case local has no meaning)
     */
    virtual surface_interface_t *map_input_coordinates(
        wf::pointf_t cursor, wf::pointf_t& local);

    /**
     * Transform the given point's coordinates into the local coordinate system
     * of the given surface in the view's surface tree, after applying all
     * transforms of the view.
     *
     * @param arg The point in global (output-local) coordinates
     * @param surface The reference surface, or null for view-local coordinates
     * @return The point in surface-local coordinates
     */
    virtual wf::pointf_t global_to_local_point(const wf::pointf_t& arg,
        surface_interface_t *surface);

    /**
     * @return the wlr_surface which should receive focus when focusing this
     * view. Views which aren't backed by a wlr_surface should implement the
     * compositor_view interface.
     *
     * In case no focus surface is available, or the view should not be focused,
     * nullptr should be returned.
     */
    virtual wlr_surface *get_keyboard_focus_surface() = 0;

    /**
     * Check whether the surface is focuseable. Note the actual ability to give
     * keyboard focus while the surface is mapped is determined by the keyboard
     * focus surface or the compositor_view implementation.
     *
     * This is meant for plugins like matcher, which need to check whether the
     * view is focuseable at any point of the view life-cycle.
     */
    virtual bool is_focuseable() const;

    /** Whether the view is in fullscreen state, usually you want to use either
     * set_fullscreen() or fullscreen_request() */
    bool fullscreen = false;
    /** Whether the view is in activated state, usually you want to use either
     * set_activated() or focus_request() */
    bool activated = false;
    /** Whether the view is in minimized state, usually you want to use either
     * set_minimized() or minimize_request() */
    bool minimized = false;
    /** Whether the view is sticky. If a view is sticky it will not be affected
     * by changes of the current workspace. */
    bool sticky = false;
    /** The tiled edges of the view, usually you want to use set_tiled().
     * If the view is tiled to all edges, it is considered maximized. */
    uint32_t tiled_edges = 0;

    /** Set the minimized state of the view. */
    virtual void set_minimized(bool minimized);
    /** Set the tiled edges of the view */
    virtual void set_tiled(uint32_t edges);
    /** Set the fullscreen state of the view */
    virtual void set_fullscreen(bool fullscreen);
    /** Set the view's activated state.  */
    virtual void set_activated(bool active);
    /** Set the view's sticky state. */
    virtual void set_sticky(bool sticky);

    /** Request that an interactive move starts for this view */
    virtual void move_request();
    /** Request that the view is focused on its output */
    virtual void focus_request();
    /** Request that an interactive resize starts for this view */
    virtual void resize_request(uint32_t edges = 0);
    /** Request that the view is (un)minimized */
    virtual void minimize_request(bool minimized);
    /**
     * Request that the view is (un)tiled.
     *
     * If the view is being tiled, the caller should ensure thaat the view is on
     * the correct workspace.
     *
     * Note: by default, any tiled edges means that the view gets the full
     * workarea.
     */
    virtual void tile_request(uint32_t tiled_edges);

    /**
     * Request that the view is (un)tiled on the given workspace.
     */
    virtual void tile_request(uint32_t tiled_edges, wf::point_t ws);

    /** Request that the view is (un)fullscreened on the given output */
    virtual void fullscreen_request(wf::output_t *output, bool state);

    /**
     * Request that the view is (un)fullscreened on the given output
     * and workspace.
     */
    virtual void fullscreen_request(wf::output_t *output, bool state,
        wf::point_t ws);

    /** @return true if the view is visible */
    virtual bool is_visible();

    /**
     * Change the view visibility. Visibility requests are counted, i.e if the
     * view is made invisible two times, it needs to be made visible two times
     * before it is visible again.
     */
    virtual void set_visible(bool visible);

    /** Damage the whole view and add the damage to its output */
    virtual void damage();

    /** @return the app-id of the view */
    virtual std::string get_app_id()
    {
        return "";
    }

    /** @return the title of the view */
    virtual std::string get_title()
    {
        return "";
    }

    /**
     * Get the minimize target for this view, i.e when displaying a minimize
     * animation, where the animation's target should be. Defaults to {0,0,0,0}.
     *
     * @return the minimize target
     */
    virtual wlr_box get_minimize_hint();

    /**
     * Sets the minimize target for this view, i.e when displaying a minimize
     * animation, where the animation's target should be.
     * @param hint The new minimize target rectangle, in output-local coordinates.
     */
    virtual void set_minimize_hint(wlr_box hint);
    /** @return true if the view needs decorations */
    virtual bool should_be_decorated();

    /**
     * Set the decoration surface for the view.
     *
     * @param frame The surface to be set as a decoration.
     *
     * The life-time of the decoration frame is managed by the view itself, so after
     * calling this function you probably want to drop any references that you
     * hold (excluding the default one)
     */
    virtual void set_decoration(std::unique_ptr<decorator_frame_t_t> frame);

    /**
     * Get the decoration frame for a view. May be nullptr.
     */
    virtual nonstd::observer_ptr<decorator_frame_t_t> get_decoration();

    /*
     *                        View transforms
     * A view transform can be any kind of transformation, for example 3D
     * rotation, wobbly effect or similar. When we speak of transforms, a
     * "view" is defined as a toplevel window (including decoration) and also
     * all of its subsurfaces/popups. The transformation then is applied to
     * this group of surfaces together.
     *
     * When a view has a custom transform, then internally all these surfaces
     * are rendered to a FBO, and then the custom transformation renders the
     * resulting texture as it sees fit. In case of multiple transforms, we do
     * multiple render passes where each transform is fed the result of the
     * previous transforms.
     *
     * Damage tracking for transformed views is done on the boundingbox of the
     * damaged region after applying the transformation, but all damaged parts
     * of the internal FBO are updated.
     * */

    void add_transformer(std::unique_ptr<wf::view_transformer_t> transformer);

    /**
     * Add a transformer with the given name. Note that you can add multiple
     * transforms with the same name.
     */
    void add_transformer(std::unique_ptr<wf::view_transformer_t> transformer,
        std::string name);

    /** @return a transformer with the give name, or null */
    nonstd::observer_ptr<wf::view_transformer_t> get_transformer(
        std::string name);

    /** remove a transformer */
    void pop_transformer(
        nonstd::observer_ptr<wf::view_transformer_t> transformer);
    /** remove all transformers with the given name */
    void pop_transformer(std::string name);
    /** @return true if the view has active transformers */
    bool has_transformer();

    /** @return the bounding box of the view up to the given transformer */
    wlr_box get_bounding_box(std::string transformer);
    /** @return the bounding box of the view up to the given transformer */
    wlr_box get_bounding_box(
        nonstd::observer_ptr<wf::view_transformer_t> transformer);

    /**
     * Transform a point with the view's transformers.
     *
     * @param point The point in output-local coordinates, before applying the
     *              view transformers.
     * @return The point in output-local coordinates after applying the
     *         view transformers.
     */
    wf::pointf_t transform_point(const wf::pointf_t& point);

    /** @return a bounding box of the given box after applying the
     * transformers of the view */
    wlr_box transform_region(const wlr_box & box);

    /** @return a bounding box of the given box after applying the transformers
     * of the view up to the given transformer */
    wlr_box transform_region(const wlr_box& box, std::string transformer);
    /** @return a bounding box of the given box after applying the transformers
     * of the view up to the given transformer */
    wlr_box transform_region(const wlr_box& box,
        nonstd::observer_ptr<wf::view_transformer_t> transformer);

    /**
     * @return true if the region intersects any of the surfaces in the view's
     * surface tree.
     */
    bool intersects_region(const wlr_box& region);

    /**
     * Get the transformed opaque region of the view and its subsurfaces.
     * The returned region is in output-local coordinates.
     */
    virtual wf::region_t get_transformed_opaque_region();

    /**
     * Render all the surfaces of the view using the view's transforms.
     * If the view is unmapped, this operation will try to read from any
     * snapshot created by take_snapshot() or when transformers were applied,
     * and use that buffer.
     *
     * Child views like dialogues are considered a part of the view's surface
     * tree, however they are not transformed by the view's transformers.
     *
     * @param framebuffer The framebuffer to render to. Geometry needs to be
     *   in output-local coordinate system.
     * @param damage The damaged region of the view, in output-local coordinate
     *   system.
     *
     * @return true if the render operation was successful, and false if the
     *   view is both unmapped and has no snapshot.
     */
    bool render_transformed(const framebuffer_t& framebuffer,
        const region_t& damage);

    /**
     * A snapshot of the view is a copy of the view's contents into a
     * framebuffer. It is used to get an image of the view while it is mapped,
     * and continue displaying it afterwards. Additionally, return the captured
     * framebuffter
     */
    virtual const wf::framebuffer_t& take_snapshot();

    /**
     * View lifetime is managed by reference counting. To take a reference,
     * use take_ref(). Note that one reference is automatically made when the
     * view is created.
     */
    void take_ref();

    /**
     * Drop a reference to the surface. When the reference count reaches 0, the
     * destruct() method is called.
     */
    void unref();

    virtual ~view_interface_t();

    view_interface_t(const view_interface_t &) = delete;
    view_interface_t(view_interface_t &&) = delete;
    view_interface_t& operator =(const view_interface_t&) = delete;
    view_interface_t& operator =(view_interface_t&&) = delete;

    class view_priv_impl;
    std::unique_ptr<view_priv_impl> view_impl;

    /**
     * The last time(nanoseconds since epoch) when the view was focused.
     * Updated automatically by core.
     */
    uint64_t last_focus_timestamp = 0;

  protected:
    view_interface_t();

    friend class compositor_core_impl_t;
    /**
     * View initialization happens in three stages:
     *
     * First, memory for the view is allocated and default members are set.
     * Second, the view is added to core, which assigns the view to an output.
     * Third, core calls @initialize() on the view. This is where the real view
     *   initialization should happen.
     *
     * Note that generally most of the operations in 3. can be also done in 1.,
     * except when they require an output.
     */
    virtual void initialize();

    /**
     * When a view is being destroyed, all associated objects like subsurfaces,
     * transformers and custom data are destroyed.
     *
     * In general, we want to make sure that these associated objects are freed
     * before the actual view object destruction starts. Thus, deinitialize()
     * is called from core just before destroying the view.
     */
    virtual void deinitialize();

    /** get_offset() is not valid for views */
    virtual wf::point_t get_offset() override
    {
        return {0, 0};
    }

    /** Damage the given box, in surface-local coordinates */
    virtual void damage_surface_box(const wlr_box& box) override;

    /**
     * @return the bounding box of the view before transformers,
     *  in output-local coordinates
     */
    virtual wf::geometry_t get_untransformed_bounding_box();


    /**
     * Called when the reference count reaches 0.
     * It destructs the object and deletes it, so "this" may not be
     * accessed after destruct() is called.
     */
    virtual void destruct();

    /**
     * Called whenever the minimized, tiled, fullscreened
     * or activated state changes */
    virtual void desktop_state_updated();

    /**
     * Emit the view map signal. It indicates that a view has been mapped, i.e.
     * plugins can now "work" with it. Note that not all views will emit the map
     * event.
     */
    virtual void emit_view_map();

    /**
     * Emit the view unmap signal. It indicates that the view is in the process of
     * being destroyed. Most plugins should stop any actions they have on the view.
     */
    virtual void emit_view_unmap();

    /**
     * Emit the view pre-unmap signal. It is emitted right before the view
     * destruction start. At this moment a plugin can still take a snapshot of the
     * view. Note that not all views emit the pre-unmap signal, however the unmap
     * signal is mandatory for all views.
     */
    virtual void emit_view_pre_unmap();
};

wayfire_view wl_surface_to_wayfire_view(wl_resource *surface);

namespace scene
{
/**
 * A node in the scenegraph representing a single view and its surfaces.
 *
 * A view is always contained in a floating_inner_node_t responsible for it
 * and its view tree. This is necessary, because that floating parent also
 * contains inner nodes for each child view (e.g. dialogs).
 *
 * An example of the structure of a main view with two dialogs,
 * one of the dialog having a nested dialog in turn, is the following:
 *
 * floating_inner_node_t(main_view):
 *   - view_node_t(main_view)
 *   - floating_inner_node_t(dialog1):
 *     - view_node_t(dialog1)
 *   - floating_inner_node_t(dialog2):
 *     - view_node_t(dialog2)
 *     - floating_inner_node_t(dialog2.1):
 *       - view_node_t(dialog2.1)
 *
 * Each view node is a structure node for its floating parent (e.g. cannot be
 * removed from it). Instead, plugins should reorder/move the view's parent node,
 * therefore ensuring that each view moves together with its children.
 */
class view_node_t final : public scene::node_t
{
  public:
    view_node_t(wayfire_view view);

    iteration visit(visitor_t *visitor) final;
    std::optional<input_node_t> find_node_at(const wf::pointf_t& at) final;
    std::string stringify() const override;

    wayfire_view get_view() const
    {
        return view;
    }

    keyboard_interaction_t& keyboard_interaction() final;

  private:
    wayfire_view view;
    std::unique_ptr<keyboard_interaction_t> kb_interaction;
};
}
}

#endif
