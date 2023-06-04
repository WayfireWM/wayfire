#ifndef VIEW_HPP
#define VIEW_HPP

#include <memory>
#include <vector>
#include <wayfire/nonstd/observer_ptr.h>

#include "wayfire/object.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/decorator.hpp"
#include "wayfire/view-transform.hpp"
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/region.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/scene.hpp>

namespace wf
{
class view_interface_t;
class workspace_set_t;
class decorator_frame_t_t;
class toplevel_t;
}

using wayfire_view = nonstd::observer_ptr<wf::view_interface_t>;

namespace wf
{
class output_t;
namespace scene
{
class view_node_t;
}

// A signal emitted when the view is destroyed and its memory will be freed.
struct view_destruct_signal
{
    wayfire_view view;
};

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
 * A list of standard actions which may be allowed on a view.
 */
enum view_allowed_actions_t
{
    // None of the actions below are allowed.
    VIEW_ALLOW_NONE      = 0,
    // It is allowed to move the view anywhere on the screen.
    VIEW_ALLOW_MOVE      = (1 << 0),
    // It is allowed to resize the view arbitrarily.
    VIEW_ALLOW_RESIZE    = (1 << 1),
    // It is allowed to move the view to another workspace.
    VIEW_ALLOW_WS_CHANGE = (1 << 2),
    // All of the actions above are allowed.
    VIEW_ALLOW_ALL       = VIEW_ALLOW_MOVE | VIEW_ALLOW_RESIZE | VIEW_ALLOW_WS_CHANGE,
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
class view_interface_t : public wf::signal::provider_t, public wf::object_base_t
{
  public:
    /**
     * Get the root of the view tree. This is the node which contains the view
     * and all of its child views.
     *
     * Usually, the tree root node has at least the transformed_node as its child,
     * and the tree root nodes of child views.
     */
    const scene::floating_inner_ptr& get_root_node() const;

    /**
     * Get the root of the view itself, including its main surface, subsurfaces
     * and transformers, but not dialogs.
     */
    std::shared_ptr<scene::transform_manager_node_t> get_transformed_node() const;

    /**
     * Get the node which contains the main view (+subsurfaces) only.
     */
    const scene::floating_inner_ptr& get_surface_root_node() const;

    /**
     * Get the toplevel object associated with the view, if it exists.
     */
    const std::shared_ptr<toplevel_t>& toplevel() const;

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
    virtual void set_output(wf::output_t *new_output);

    /**
     * Get the view's main output.
     */
    virtual wf::output_t *get_output();

    /**
     * Get the workspace set the view is attached to, if any.
     */
    std::shared_ptr<workspace_set_t> get_wset();

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
     * Get the allowed actions for this view. By default, all actions are allowed, but plugins may disable
     * individual actions.
     *
     * The allowed actions are a bitmask of @view_allowed_actions_t.
     */
    uint32_t get_allowed_actions() const;

    /**
     * Set the allowed actions for the view.
     *
     * @param actions The allowed actions, a bitmask of @view_allowed_actions_t.
     */
    void set_allowed_actions(uint32_t actions) const;

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
     * @return the wlr_surface which should receive focus when focusing this
     * view. Views which aren't backed by a wlr_surface should implement the
     * compositor_view interface.
     *
     * In case no focus surface is available, or the view should not be focused,
     * nullptr should be returned.
     */
    virtual wlr_surface *get_keyboard_focus_surface() = 0;

    /**
     * Check whether the surface is focusable. Note the actual ability to give
     * keyboard focus while the surface is mapped is determined by the keyboard
     * focus surface or the compositor_view implementation.
     *
     * This is meant for plugins like matcher, which need to check whether the
     * view is focusable at any point of the view life-cycle.
     */
    virtual bool is_focusable() const;

    /** Whether the view is in fullscreen state, usually you want to use either
     * set_fullscreen() or fullscreen_request() */
    bool fullscreen = false;
    /** Whether the view is in activated state, usually you want to use either
     * set_activated() or focus_request() */
    bool activated = false;
    /** Whether the view is in minimized state, usually you want to use either
     * set_minimized() or minimize_request() */
    bool minimized = false;
    bool pending_minimized = false;
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

    /** @return true if the view has active transformers */
    bool has_transformer();

    /**
     * A snapshot of the view is a copy of the view's contents into a framebuffer.
     */
    virtual void take_snapshot(wf::render_target_t& target);

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

    /**
     * @return the wl_client associated with this surface, or null if the
     *   surface doesn't have a backing wlr_surface.
     */
    wl_client *get_client();

    /**
     * @return the wlr_surface associated with this surface, or null if no
     *   the surface doesn't have a backing wlr_surface. */
    wlr_surface *get_wlr_surface();

    virtual bool is_mapped() const
    {
        return false;
    }

    virtual ~view_interface_t();

    class view_priv_impl;
    std::unique_ptr<view_priv_impl> priv;

  protected:
    view_interface_t();
    void set_surface_root_node(scene::floating_inner_ptr surface_root_node);

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

/**
 * Find a view this node belongs to.
 * May return NULL if @node is NULL or it is not a child of a view node.
 */
wayfire_view node_to_view(wf::scene::node_ptr node);
wayfire_view node_to_view(wf::scene::node_t *node);

/**
 * A base class for nodes which are to be identified as views.
 * Used by @node_to_view in order to figure out whether a given node is a view or not.
 */
class view_node_tag_t
{
  public:
    virtual ~view_node_tag_t() = default;
    virtual wayfire_view get_view() const = 0;
};

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
 * therefore ensuring that each view moves in the scenegraph together with its
 * children.
 *
 * Each view_node_t also exposes its surfaces as children.
 * The children's coordinate system is transformed to match the transformers
 * currently applied on the view and then offsetted to start at the top-left
 * corner of the view's main surface.
 */
class view_node_t : public scene::floating_inner_node_t,
    public zero_copy_texturable_node_t, public view_node_tag_t
{
  public:
    view_node_t(wayfire_view view);
    std::optional<input_node_t> find_node_at(const wf::pointf_t& at) override;
    wf::keyboard_focus_node_t keyboard_refocus(wf::output_t *output) override;
    std::string stringify() const override;

    wf::pointf_t to_local(const wf::pointf_t& point) override;
    wf::pointf_t to_global(const wf::pointf_t& point) override;

    wayfire_view get_view() const override
    {
        return view;
    }

    wf::region_t get_opaque_region() const;

    keyboard_interaction_t& keyboard_interaction() override;

    /**
     * Views currently gather damage, etc. manually from the surfaces,
     * and sometimes render them, sometimes not ...
     */
    void gen_render_instances(
        std::vector<render_instance_uptr>& instances,
        damage_callback push_damage,
        wf::output_t *output) override;

    std::optional<wf::texture_t> to_texture() const override;
    wf::geometry_t get_bounding_box() override;

  protected:
    view_node_t();
    wayfire_view view;
    std::unique_ptr<keyboard_interaction_t> kb_interaction;
    wf::signal::connection_t<view_destruct_signal> on_view_destroy;
};
}
}

#endif
