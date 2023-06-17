#pragma once

#include "wayfire/nonstd/observer_ptr.h"
#include <wayfire/view.hpp>
#include <wayfire/toplevel.hpp>

namespace wf
{
class toplevel_t;
class toplevel_view_interface_t;
class window_manager_t;
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

    /**
     * A wrapper function for updating the toplevel's position.
     * Equivalent to setting the pending coordinates of the toplevel and committing it in a new transaction.
     */
    void move(int x, int y);

    /**
     * A wrapper function for updating the toplevel's dimensions.
     * Equivalent to setting the pending dimensions of the toplevel and committing it in a new transaction.
     */
    void resize(int w, int h);

    /**
     * A wrapper function for updating the toplevel's geometry.
     * Equivalent to setting the pending geometry of the toplevel and committing it in a new transaction.
     */
    void set_geometry(wf::geometry_t g);

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

    /** Set the minimized state of the view. */
    virtual void set_minimized(bool minimized);
    /** Set the view's activated state.  */
    virtual void set_activated(bool active);
    /** Set the view's sticky state. */
    virtual void set_sticky(bool sticky);

    inline uint32_t pending_tiled_edges()
    {
        return toplevel()->pending().tiled_edges;
    }

    inline bool pending_fullscreen()
    {
        return toplevel()->pending().fullscreen;
    }

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
