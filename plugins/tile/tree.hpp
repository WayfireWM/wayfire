#ifndef WF_TILE_PLUGIN_TREE
#define WF_TILE_PLUGIN_TREE

#include "wayfire/signal-definitions.hpp"
#include "wayfire/workspace-set.hpp"
#include <wayfire/util/duration.hpp>
#include <wayfire/view.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/txn/transaction.hpp>

namespace wf
{
namespace tile
{
/**
 * A tree node represents a logical container of views in the tiled part of
 * a workspace.
 *
 * There are two types of nodes:
 * 1. View tree nodes, i.e leaves, they contain a single view
 * 2. Split tree nodes, they contain at least 1 child view.
 */
struct split_node_t;
struct view_node_t;

struct gap_size_t
{
    /* Gap on the left side */
    int32_t left = 0;
    /* Gap on the right side */
    int32_t right = 0;
    /* Gap on the top side */
    int32_t top = 0;
    /* Gap on the bottom side */
    int32_t bottom = 0;
    /* Gap for internal splits */
    int32_t internal = 0;
};

struct tree_node_t
{
    /** The node parent, or nullptr if this is the root node */
    nonstd::observer_ptr<split_node_t> parent;

    /** The children of the node */
    std::vector<std::unique_ptr<tree_node_t>> children;

    /** The geometry occupied by the node */
    wf::geometry_t geometry;

    /** Set the geometry available for the node and its subnodes. */
    virtual void set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx);

    /** Set the gaps for the node and subnodes. */
    virtual void set_gaps(const gap_size_t& gaps, wf::txn::transaction_uptr& tx) = 0;

    virtual ~tree_node_t()
    {}

    /** Simply dynamic cast this to a split_node_t */
    nonstd::observer_ptr<split_node_t> as_split_node();
    /** Simply dynamic cast this to a view_node_t */
    nonstd::observer_ptr<view_node_t> as_view_node();

  protected:
    /* Gaps */
    gap_size_t gaps;
};

/**
 * A node which contains a split can be split either horizontally or vertically
 */
enum split_direction_t
{
    SPLIT_HORIZONTAL = 0,
    SPLIT_VERTICAL   = 1,
};

/*
 * Represents a node in the tree which contains at 1 one child node
 */
struct split_node_t : public tree_node_t
{
    /**
     * Add the given child to the list of children.
     *
     * The new child will get resized so that its area is at most 1/(N+1) of the
     * total node area, where N is the number of children before adding the new
     * child.
     *
     * @param index The index at which to insert the new child, or -1 for
     *              adding to the end of the child list.
     */
    void add_child(std::unique_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx, int index = -1);

    /**
     * Remove a child from the node, and return its unique_ptr
     */
    std::unique_ptr<tree_node_t> remove_child(
        nonstd::observer_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx);

    /**
     * Set the total geometry available to the node. This will recursively
     * resize the children nodes, so that they fit inside the new geometry and
     * have a size proportional to their old size.
     */
    void set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx) override;

    /**
     * Set the gaps for the subnodes. The internal gap will override
     * the corresponding edges for each child.
     */
    void set_gaps(const gap_size_t& gaps, wf::txn::transaction_uptr& tx) override;

    split_node_t(split_direction_t direction);
    split_direction_t get_split_direction() const;

  private:
    split_direction_t split_direction;

    /**
     * Resize the children so that they fit inside the given
     * available_geometry.
     */
    void recalculate_children(wf::geometry_t available_geometry, wf::txn::transaction_uptr& tx);

    /**
     * Calculate the geometry of a child if it has child_size as one
     * dimension. Whether this is width/height depends on the node split type.
     *
     * @param child_pos The position from which the child starts, relative to
     *                  the node itself
     *
     * @return The geometry of the child, in global coordinates
     */
    wf::geometry_t get_child_geometry(int32_t child_pos, int32_t child_size);

    /** Return the size of the node in the dimension in which the split happens */
    int32_t calculate_splittable() const;
    /** Return the size of the geometry in the dimension in which the split
     * happens */
    int32_t calculate_splittable(wf::geometry_t geometry) const;
};

struct tile_adjust_transformer_signal
{};

/**
 * Represents a leaf in the tree, contains a single view
 */
struct view_node_t : public tree_node_t
{
    view_node_t(wayfire_toplevel_view view);
    ~view_node_t();

    wayfire_toplevel_view view;
    /**
     * Set the geometry of the node and the contained view.
     *
     * Note that the resulting view geometry will not always be equal to the
     * geometry of the node. For example, a fullscreen view will always have
     * the geometry of the whole output.
     */
    void set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx) override;

    /**
     * Set the gaps for non-fullscreen mode.
     * The gap sizes will be subtracted from all edges of the view's geometry.
     */
    void set_gaps(const gap_size_t& gaps, wf::txn::transaction_uptr& tx) override;

    /* Return the tree node corresponding to the view, or nullptr if none */
    static nonstd::observer_ptr<view_node_t> get_node(wayfire_view view);

  private:
    struct scale_transformer_t;
    nonstd::observer_ptr<scale_transformer_t> transformer;

    wf::signal::connection_t<view_geometry_changed_signal> on_geometry_changed;
    wf::signal::connection_t<tile_adjust_transformer_signal> on_adjust_transformer;

    wf::option_wrapper_t<wf::animation_description_t> animation_duration{"simple-tile/animation_duration"};

    /**
     * Check whether the crossfade animation should be enabled for the view
     * currently.
     */
    bool needs_crossfade();

    wf::geometry_t calculate_target_geometry();
    void update_transformer();
};

/**
 * Flatten the tree as much as possible, i.e remove nodes with only one
 * split-node child.
 *
 * The only exception is "the root", which will always be a split node.
 *
 * Note: this will potentially invalidate pointers to the tree and modify
 * the given parameter.
 */
void flatten_tree(std::unique_ptr<tree_node_t>& root, wf::txn::transaction_uptr& tx);

/**
 * Get the root of the tree which node is part of
 */
nonstd::observer_ptr<split_node_t> get_root(nonstd::observer_ptr<tree_node_t> node);

/**
 * Transform coordinates from the tiling trees coordinate system to wset-local coordinates.
 */
wf::geometry_t get_wset_local_coordinates(std::shared_ptr<wf::workspace_set_t> wset, wf::geometry_t g);
wf::point_t get_wset_local_coordinates(std::shared_ptr<wf::workspace_set_t> wset, wf::point_t g);

// Since wsets may not have been attached to any output yet, they may not have a native 'resolution'.
// In this case, we use a default resolution of 1920x1080 in order to layout views. This resolution will be
// automatically adjusted once the wset is added to an output.
static constexpr wf::geometry_t default_output_resolution = {0, 0, 1920, 1080};
}
}

#endif /* end of include guard: WF_TILE_PLUGIN_TREE */
