#ifndef WORKSPACE_MANAGER_HPP
#define WORKSPACE_MANAGER_HPP

#include <functional>
#include <vector>
#include <wayfire/view.hpp>

namespace wf
{
/**
 * The workspace implementation is a way for plugins to request more detailed
 * control over what happens on the given workspace. For example a tiling
 * plugin would disable move and/or resize operations for some views.
 */
struct workspace_implementation_t
{
    virtual bool view_movable(wayfire_view view)   = 0;
    virtual bool view_resizable(wayfire_view view) = 0;
    virtual ~workspace_implementation_t()
    {}
};

/**
 * A set of flags that can be ORed and used as flags for the workspace set's get_view() function.
 */
enum wset_view_flags
{
    // Include mapped views only.
    WSET_MAPPED_ONLY       = (1 << 0),
    // Exclude minimized views, they are included by default.
    WSET_EXCLUDE_MINIMIZED = (1 << 1),
    // Views on the current workspace only, a shorthand for requesting the current workspace and supplying it
    // as the second filter of get_views().
    WSET_CURRENT_WORKSPACE = (1 << 2),
    // Sort the resulting array in the same order as the scenegraph nodes of the corresponding views.
    // Views not attached to the scenegraph (wf::get_core().scene()) are not included in the answer.
    // This operation may be slow, so it should not be used on hot paths.
    WSET_SORT_STACKING     = (1 << 3),
};

/**
 * Workspace manager is responsible for managing the layers, the workspaces and
 * the views in them. There is one workspace manager per output.
 *
 * In the default workspace_manager implementation, there is one set of layers
 * per output. Each layer is infinite and covers all workspaces.
 *
 * Each output also has a set of workspaces, arranged in a 2D grid. A view may
 * overlap multiple workspaces.
 */
class workspace_set_t
{
  public:
    /**
     * Get the scenegraph node belonging to the workspace set.
     * Each workspace set has one scenegraph node which is put in the workspace layer and contains most of
     * the views from the workspace set. It is nonetheless possible to add views which are placed elsewhere
     * in the scenegraph (for example, on a different layer).
     */
    scene::floating_inner_ptr get_node() const;

    /**
     * Add the given view to the workspace set.
     * Until the view is removed, it will be counted as part of the workspace set.
     * This means that it will be moved when the workspace changes, and it will be part of the view list
     * returned by @get_views().
     *
     * The workspace set is also responsible for associating the view with an output, in case the workspace
     * set is moved to a different output.
     *
     * Note that adding a view to the workspace set does not automatically add the view to the scenegraph.
     * The stacking order, layer information, etc. is all determined by the scenegraph and managed separately
     * from the workspace set, which serves an organizational purpose.
     */
    void add_view(wayfire_view view);

    /**
     * Remove the view from the workspace set.
     * Note that the view will remain associated with the last output the workspace set was on.
     */
    void remove_view(wayfire_view view);

    /**
     * Get a list of all views currently in the workspace set.
     *
     * Note that the list is not sorted by default (use WSET_SORT_STACKING if sorting is needed), and may
     * contain views from different scenegraph layers.
     *
     * @param flags A bit mask of wset_view_flags. See the individual enum values for detailed description.
     * @param workspace An optional workspace to filter views for (i.e. only views from that workspace will be
     *   included in the return value. WSET_CURRENT_WORKSPACE takes higher precedence than this value if
     *   specified.
     */
    std::vector<wayfire_view> get_views(uint32_t flags = 0, std::optional<wf::point_t> workspace = {});

    /**
     * Get the main workspace for a view.
     * The main workspace is the one which contains the view's center.
     *
     * If the center is on an invalid workspace, the closest workspace will be returned.
     */
    wf::point_t get_view_main_workspace(wayfire_view view);

    /**
     * Check if the given view is visible on the given workspace
     */
    bool view_visible_on(wayfire_view view, wf::point_t ws);

    /**
     * Ensure that the view's wm_geometry is visible on the workspace ws. This
     * involves moving the view as appropriate.
     */
    void move_to_workspace(wayfire_view view, wf::point_t ws);

    /**
     * @return The current workspace implementation
     */
    workspace_implementation_t *get_workspace_implementation();

    /**
     * Set the active workspace implementation
     * @param impl - The workspace implementation, or null if default
     * @param overwrite - Whether to set the implementation even if another
     *        non-default implementation has already been set.
     *
     * @return true iff the implementation has been set.
     */
    bool set_workspace_implementation(
        std::unique_ptr<workspace_implementation_t> impl,
        bool overwrite = false);

    /**
     * Directly change the active workspace.
     *
     * @param ws The new active workspace.
     * @param fixed_views Views which do not change their workspace relative
     *   to the current workspace (together with their child views). Note that it
     *   may result in views getting offscreen if they are not visible on the
     *   current workspace.
     */
    void set_workspace(wf::point_t ws,
        const std::vector<wayfire_view>& fixed_views = {});

    /**
     * Switch to the given workspace.
     * If possible, use a plugin which provides animation.
     *
     * @param ws The new active workspace.
     * @param fixed_views Views which do not change their workspace relative
     *   to the current workspace (together with their child views). See also
     *   workspace-change-request-signal.
     */
    void request_workspace(wf::point_t ws,
        const std::vector<wayfire_view>& fixed_views = {});

    /**
     * @return The given workspace
     */
    wf::point_t get_current_workspace();

    /**
     * @return The number of workspace columns and rows
     */
    wf::dimensions_t get_workspace_grid_size();

    /**
     * Set the workspace grid size for this output.
     *
     * Once a plugin calls this, the number of workspaces will no longer be
     * updated according to the config file.
     */
    void set_workspace_grid_size(wf::dimensions_t grid_size);

    /**
     * @return Whether the given workspace is valid
     */
    bool is_workspace_valid(wf::point_t ws);

    workspace_set_t(output_t *output);
    ~workspace_set_t();

  protected:
    class impl;
    std::unique_ptr<impl> pimpl;
};
}

#endif /* end of include guard: WORKSPACE_MANAGER_HPP */
