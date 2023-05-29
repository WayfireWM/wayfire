#ifndef WORKSPACE_MANAGER_HPP
#define WORKSPACE_MANAGER_HPP

#include "wayfire/object.hpp"
#include "wayfire/signal-provider.hpp"
#include <functional>
#include <vector>
#include <wayfire/view.hpp>

namespace wf
{
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
class workspace_set_t : public wf::signal::provider_t, public wf::object_base_t
{
  public:
    /**
     * Create a new empty workspace set. By default, the workspace set uses the core/{vwidth,vheight} options
     * to determine the workspace grid dimensions and is not attached to any outputs.
     *
     * When first created, the workspace set is invisible. It may become visible when it is set as the current
     * workspace set on an output.
     *
     * @param index The index of the new workspace set. It will be used if available, otherwise, a the lowest
     *   available index will be selected (starting from 1).
     */
    workspace_set_t(int64_t index = -1);
    ~workspace_set_t();

    /**
     * Get the index of the workspace set. The index is assigned on creation and always the lowest unused
     * index is assigned to the new set.
     */
    uint64_t get_index() const;

    /**
     * Attach the workspace set to the given output.
     * Note that this does not automatically make the workspace set visible on the output, it also needs to be
     * set as the current workspace set on it.
     *
     * @param output The new output to attach to, or nullptr.
     */
    void attach_to_output(wf::output_t *output);

    /**
     * Get the currently attached output, or null.
     */
    wf::output_t *get_attached_output();

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
     *
     * Note 2: Special care should be taken when adding views that are not part of the default scenegraph
     * node of the workspace set (i.e. @get_node()). Plugins adding these views have to ensure that the views
     * are disabled if the workspace set is not active on any output.
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

  private:
    struct impl;
    std::unique_ptr<impl> pimpl;
    friend class output_impl_t;

    /**
     * Change the visibility of the workspace set. On each output, only one workspace set will be visible
     * (the current workspace set). When a workspace set is invisible, views in it will be disabled in the
     * scenegraph.
     */
    void set_visible(bool visible);
};
}

#endif /* end of include guard: WORKSPACE_MANAGER_HPP */
