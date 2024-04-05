#ifndef WF_TILE_PLUGIN_TREE_CONTROLLER_HPP
#define WF_TILE_PLUGIN_TREE_CONTROLLER_HPP

#include "tree.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include <wayfire/option-wrapper.hpp>
#include <wayfire/plugins/common/move-drag-interface.hpp>

/* Contains functions which are related to manipulating the tiling tree */
namespace wf
{
class preview_indication_t;
namespace tile
{
/**
 * Run callback for each view in the tree
 */
void for_each_view(nonstd::observer_ptr<tree_node_t> root,
    std::function<void(wayfire_toplevel_view)> callback);

enum split_insertion_t
{
    /** Insert is invalid */
    INSERT_NONE  = 0,
    /** Insert above the view */
    INSERT_ABOVE = 1,
    /** Insert below the view */
    INSERT_BELOW = 2,
    /** Insert to the left of the view */
    INSERT_LEFT  = 3,
    /** Insert to the right of the view */
    INSERT_RIGHT = 4,
    /** Insert by swapping with the source view */
    INSERT_SWAP  = 5,
};

/**
 * Find the first view in the indicated direction
 */
nonstd::observer_ptr<view_node_t> find_first_view_in_direction(
    nonstd::observer_ptr<tree_node_t> from, split_insertion_t direction);

/**
 * Represents the current mode in which the tile plugin is.
 *
 * Invariant: while a controller is active, the tree structure shouldn't change,
 * except for changes by the controller itself.
 *
 * If such an external event happens, then controller will be destroyed.
 */
class tile_controller_t
{
  public:
    virtual ~tile_controller_t() = default;

    /** Called when the input is moved */
    virtual void input_motion()
    {}

    /**
     * Called when the input is released or the controller should stop
     * Note that a controller may be deleted without receiving input_released(),
     * in which case it should simply stop operation.
     */
    virtual void input_released(bool force_stop)
    {}
};

std::unique_ptr<tree_node_t>& get_root(wf::workspace_set_t *set, wf::point_t workspace);

/**
 * Represents the moving view action, i.e dragging a window to change its
 * position in the grid
 */
class move_view_controller_t : public tile_controller_t
{
  public:
    /**
     * Start the drag-to-reorder action.
     */
    move_view_controller_t(wf::workspace_set_t *wset, wayfire_toplevel_view view);
    ~move_view_controller_t();

    void input_motion() override;
    void input_released(bool force_stop) override;

  protected:
    wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> drag_helper;
};

class resize_view_controller_t : public tile_controller_t
{
  public:
    /**
     * Start the drag-to-resize action.
     */
    resize_view_controller_t(wf::workspace_set_t *wset, wayfire_toplevel_view view);
    ~resize_view_controller_t();

    void input_motion() override;

  protected:
    wf::output_t *output;

    /** Last input event location */
    wf::point_t last_point;

    /** Edges of the grabbed view that we're resizing */
    uint32_t resizing_edges;
    /** Calculate the resizing edges for the grabbing view. */
    uint32_t calculate_resizing_edges(wf::point_t point);

    /** The view we are resizing */
    nonstd::observer_ptr<view_node_t> grabbed_view;

    /*
     * A resizing pair of nodes is a pair of nodes we need to resize
     * The first one is always to the left/above the second one.
     */
    using resizing_pair_t = std::pair<nonstd::observer_ptr<tree_node_t>,
        nonstd::observer_ptr<tree_node_t>>;

    /** The horizontally-aligned pair we're resizing */
    resizing_pair_t horizontal_pair;
    /** The vertically-aligned pair we're resizing */
    resizing_pair_t vertical_pair;

    /*
     * Find a resizing pair in the given direction.
     *
     * The resizing pair depends on the currently grabbed view and the
     * resizing edges.
     */
    resizing_pair_t find_resizing_pair(bool horizontal);

    /**
     * Adjust the given positions and sizes while resizing.
     *
     * @param x1 The start of the first geometry
     * @param len1 The dimension of the first geometry
     * @param x2 The start of the second geometry, should be x1 + len1
     * @param len2 The length of the second geometry
     *
     * @param delta How much change to apply
     */
    void adjust_geometry(int32_t& x1, int32_t& len1,
        int32_t& x2, int32_t& len2, int32_t delta);
};

/**
 * Calculate which view node is at the given position
 *
 * Returns null if no view nodes are present.
 */
nonstd::observer_ptr<view_node_t> find_view_at(nonstd::observer_ptr<tree_node_t> root, wf::point_t input);

/**
 * Translate coordinates from output-local coordinates to the coordinate
 * system of the tiling trees, depending on the current workspace
 */
wf::point_t get_global_input_coordinates(wf::output_t *output);
}
}

#endif /* end of include guard: WF_TILE_PLUGIN_TREE_CONTROLLER_HPP */
