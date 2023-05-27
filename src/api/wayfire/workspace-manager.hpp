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
class workspace_manager
{
  public:
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

    workspace_manager(output_t *output);
    ~workspace_manager();

  protected:
    class impl;
    std::unique_ptr<impl> pimpl;
};

/**
 * Each output has a workarea manager which keeps track of the available workarea on that output. The
 * available area is typically the full output area minus any space reserved for panels, bars, etc.
 */
class output_workarea_manager_t
{
  public:
    /**
     * Special clients like panels can reserve place from an edge of the output.
     * It is used when calculating the dimensions of maximized/tiled windows and
     * others. The remaining space (which isn't reserved for panels) is called
     * the workarea.
     */
    enum anchored_edge
    {
        ANCHORED_EDGE_TOP    = 0,
        ANCHORED_EDGE_BOTTOM = 1,
        ANCHORED_EDGE_LEFT   = 2,
        ANCHORED_EDGE_RIGHT  = 3,
    };

    struct anchored_area
    {
        /* The edge from which to reserver area */
        anchored_edge edge;
        /* Amount of space to reserve */
        int reserved_size;

        /* Desired size, to be given later in the reflowed callback */
        int real_size;

        /* The reflowed callbacks allows the component registering the
         * anchored area to be notified whenever the dimensions or the position
         * of the anchored area changes.
         *
         * The first passed geometry is the geometry of the anchored area. The
         * second one is the available workarea at the moment that the current
         * workarea was considered. */
        std::function<void(wf::geometry_t, wf::geometry_t)> reflowed;
    };

    /**
     * Add a reserved area. The actual recalculation must be manually
     * triggered by calling reflow_reserved_areas()
     */
    void add_reserved_area(anchored_area *area);

    /**
     * Remove a reserved area. The actual recalculation must be manually
     * triggered by calling reflow_reserved_areas()
     */
    void remove_reserved_area(anchored_area *area);

    /**
     * Recalculate reserved area for each anchored area
     */
    void reflow_reserved_areas();

    /**
     * @return The free space of the output after reserving the space for panels
     */
    wf::geometry_t get_workarea();

    output_workarea_manager_t(wf::output_t *output);
    ~output_workarea_manager_t();

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}

#endif /* end of include guard: WORKSPACE_MANAGER_HPP */
