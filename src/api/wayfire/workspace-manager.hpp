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
 * Wayfire organizes views into several layers, in order to simplify z ordering.
 */
enum layer_t
{
    /* The lowest layer, typical clients here are backgrounds */
    LAYER_BACKGROUND     = (1 << 0),
    /* The bottom layer */
    LAYER_BOTTOM         = (1 << 1),
    /* The workspace layer is where regular views are placed */
    LAYER_WORKSPACE      = (1 << 2),
    /* The top layer. Typical clients here are non-autohiding panels */
    LAYER_TOP            = (1 << 3),
    /* The unmanaged layer contains views like Xwayland OR windows and xdg-popups */
    LAYER_UNMANAGED      = (1 << 4),
    /* The lockscreen layer, typically lockscreens or autohiding panels */
    LAYER_LOCK           = (1 << 5),
    /* The layer where "desktop widgets" are positioned, for example an OSK
     * or a sound control popup */
    LAYER_DESKTOP_WIDGET = (1 << 6),
};

constexpr int TOTAL_LAYERS = 7;

/* The layers where regular views are placed */
constexpr int WM_LAYERS = (wf::LAYER_WORKSPACE);
/* All layers which are used for regular clients */
constexpr int MIDDLE_LAYERS = (wf::WM_LAYERS | wf::LAYER_UNMANAGED);
/* All layers which typically sit on top of other layers */
constexpr int ABOVE_LAYERS = (wf::LAYER_TOP | wf::LAYER_LOCK |
    wf::LAYER_DESKTOP_WIDGET);
/* All layers which typically sit below other layers */
constexpr int BELOW_LAYERS = (wf::LAYER_BACKGROUND | wf::LAYER_BOTTOM);

/* All visible layers */
constexpr int VISIBLE_LAYERS = (wf::MIDDLE_LAYERS | wf::ABOVE_LAYERS |
    wf::BELOW_LAYERS);
/* All layers */
constexpr int ALL_LAYERS = wf::VISIBLE_LAYERS;

/**
 * @return A bitmask consisting of all layers which are not below the given layer
 */
uint32_t all_layers_not_below(uint32_t layer);

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
     * Calculate a list of workspaces the view is visible on.
     *
     * @param threshold How much of the view's area needs to overlap a workspace to
     *   be counted as visible on it. 1.0 for 100% visible, 0.1 for 10%.
     *
     * @return a vector of all the workspaces
     */
    std::vector<wf::point_t> get_view_workspaces(wayfire_view view,
        double threshold);

    /**
     * Get the main workspace for a view.
     * The main workspace is the one which contains the view's center.
     *
     * If the center is on an invalid workspace, the closest workspace will
     * be returned.
     */
    wf::point_t get_view_main_workspace(wayfire_view view);

    /**
     * Check if the given view is visible on the given workspace
     */
    bool view_visible_on(wayfire_view view, wf::point_t ws);

    /**
     * Get a list of all views visible on the given workspace.
     * The views are returned from the topmost to the bottomost in the stacking
     * order. The stacking order is the same as in get_views_in_layer().
     *
     * @param layer_mask - The layers whose views should be included
     */
    std::vector<wayfire_view> get_views_on_workspace(wf::point_t ws,
        uint32_t layer_mask, bool include_minimized = false);

    /**
     * Ensure that the view's wm_geometry is visible on the workspace ws. This
     * involves moving the view as appropriate.
     */
    void move_to_workspace(wayfire_view view, wf::point_t ws);

    /**
     * Add the given view to the given layer. If the view was already added to
     * a (sub)layer, it will be first removed from the old one.
     *
     * Note: the view will also get its own mini-sublayer internally, because
     * each view needs to be in a sublayer.
     *
     * Preconditions: the view must have the same output as the current one
     */
    void add_view(wayfire_view view, layer_t layer);

    /**
     * Bring the sublayer of the view to the top if possible, and then bring
     * the view to the top of its sublayer.
     *
     * No-op if the view isn't in any layer.
     */
    void bring_to_front(wayfire_view view);

    /**
     * Remove the view from its (sub)layer. This effectively means that the view is
     * now invisible on the output.
     */
    void remove_view(wayfire_view view);

    /**
     * Generate a list of views in the given layers ordered in their stacking
     * order. The stacking order is usually determined by the layer and sublayer
     * ordering, however, fullscreen views which are on the top of the workspace
     * floating layer or are docked above it are reodered to be on top of the
     * panel layer (but still below the unmanaged layer).
     *
     * Whenever the aforementioned reordering happens, the
     * fullscreen-layer-focused is emitted.
     */
    std::vector<wayfire_view> get_views_in_layer(uint32_t layers_mask,
        bool include_minimized = false);

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
