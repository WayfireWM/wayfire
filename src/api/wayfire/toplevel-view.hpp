#pragma once

#include "wayfire/nonstd/observer_ptr.h"
#include <wayfire/view.hpp>

namespace wf
{
class toplevel_t;
class toplevel_view_interface_t;
}

using wayfire_toplevel_view = nonstd::observer_ptr<wf::toplevel_view_interface_t>;

namespace wf
{
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
 * Toplevel views are a subtype of views which have an associated toplevel object. As such, they may be moved,
 * resized, etc. freely by plugins and have many additional operations when compared to other view types.
 */
class toplevel_view_interface_t : public wf::view_interface_t
{
  public:
    /**
     * Get the toplevel object associated with the view.
     */
    const std::shared_ptr<toplevel_t>& toplevel() const;

    /**
     * The toplevel parent of the view, for ex. the main view of a file chooser
     * dialogue.
     */
    wayfire_toplevel_view parent = nullptr;

    /**
     * A list of the children views (typically dialogs).
     */
    std::vector<wayfire_toplevel_view> children;

    /**
     * Set the toplevel parent of the view, and adjust the children's list of
     * the parent.
     */
    void set_toplevel_parent(wayfire_toplevel_view parent);

    /**
     * Generate a list of all views in the view's tree.
     * This includes the view itself, its @children and so on.
     *
     * @param mapped_only Whether to include only mapped views.
     *
     * @return A list of all views in the view's tree. This includes the view
     *   itself, its @children and so on.
     */
    std::vector<wayfire_toplevel_view> enumerate_views(bool mapped_only = true);

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

    /**
     * The wm geometry of the view is the portion of the view surface that
     * contains the actual contents, for example, without the view shadows, etc.
     *
     * @return The wm geometry of the view.
     */
    virtual wf::geometry_t get_wm_geometry();

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
    virtual void fullscreen_request(wf::output_t *output, bool state, wf::point_t ws);

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
     * Set the view's output.
     *
     * If the new output is different from the previous, the view will be
     * removed from the layer it was on the old output.
     */
    virtual void set_output(wf::output_t *new_output) override;

    /**
     * Get the workspace set the view is attached to, if any.
     */
    std::shared_ptr<workspace_set_t> get_wset();

    virtual ~toplevel_view_interface_t();

  protected:
    /**
     * When a view is being destroyed, all associated objects like subsurfaces,
     * transformers and custom data are destroyed.
     *
     * In general, we want to make sure that these associated objects are freed
     * before the actual view object destruction starts. Thus, deinitialize()
     * is called from core just before destroying the view.
     */
    void deinitialize() override;
};

inline wayfire_toplevel_view toplevel_cast(wayfire_view view)
{
    return dynamic_cast<toplevel_view_interface_t*>(view.get());
}
}
