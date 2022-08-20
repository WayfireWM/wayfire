#pragma once

#include <optional>
#include <memory>
#include <vector>
#include <map>
#include <wayfire/geometry.hpp>
#include <wayfire/region.hpp>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/scene-input.hpp>

namespace wf
{
class surface_interface_t;
class output_t;
class output_layout_t;
/**
 * Contains definitions of the common elements used in Wayfire's scenegraph.
 *
 * The scenegraph is a complete representation of the current rendering and input
 * state of Wayfire. The basic nodes forms a tree where every node is responsible
 * for managing its children's state.
 *
 * The rough structure of the scenegraph is as follows:
 *
 * Level 1: the root node, which is a simple container of other nodes.
 * Level 2: a list of layer nodes, which represent different types of content,
 *   ordered in increasing stacking order (i.e. first layer is the bottommost).
 * Level 3: in each layer, there is a special output node for each currently
 *   enabled output. By default, this node's bounding box is limited to the
 *   extents of the output, so that no nodes overlap multiple outputs.
 * Level 4: in each output node, there is a static and a dynamic container.
 *   Static containers contain views which do not change when workspace sets
 *   are changed, for example layer-shell views. Dynamic containers contain
 *   the views which are bound to the current workspace set.
 * Level 5 and beyond: These levels typically contain views and group of views,
 *   or special effects (particle systems and the like).
 *
 * Each level may contain additional nodes added by plugins (or by core in the
 * case of DnD views). The scenegraph generally allows full flexibility here,
 * but the aforementioned nodes are always available and used by most plugins
 * to ensure the most compatibility.
 *
 * The most common operations that a plugin needs to execute on the scenegraph
 * are reordering elements (and thus changing the stack order) and potentially
 * moving them between layers and outputs. In addition, the scenegraph can be
 * used in some more advanced cases:
 *
 * - The scenegraph may be used to implement what used to be custom renderers
 *   prior to Wayfire 0.8.0, i.e. override the default output of a single
 *   workspace covering the whole output. The preferred way to do that is to
 *   disable the output nodes in each layer and add a custom node in one of the
 *   layers which does the custom rendering and covers the whole output.
 *
 * - A similar 'trick' can be used for grabbing all input on a particular output
 *   and is the preferred way to do what input grabs used to do prior to Wayfire
 *   0.8.0. To emulate a grab, create an input-only scene node in the OVERRIDE
 *   layer on an output (it thus gets all touch and pointer input automatically)
 *   and make it use an exclusive keyboard input mode to grab the keyboard as
 *   well.
 *
 * - Always-on-top views are simply nodes which are placed above the dynamic
 *   container of the workspace layer of each output.
 *
 * Implementation notes:
 *
 * - Every node has output-layout coordinates. That means that coordinates are
 *   relative to the global coordinate system where input events are handled
 *   and where outputs are put relative to each other. Each output node by
 *   default limits its surfaces to the area actually visible on that output.
 *   Therefore, even though nodes appear (by using a global scenegraph and
 *   global coordinates) to be on another output, they are actually only visible
 *   on the workspaces of their own output. Another consequence of this is that
 *   views change their coordinates when the workspace of an output changes.
 */
namespace scene
{
class node_t;
class inner_node_t;
using node_ptr = std::shared_ptr<node_t>;
using inner_node_ptr = std::shared_ptr<inner_node_t>;

class visitor_t;

/**
 * Describes which nodes to visit after the current node.
 */
enum class iteration
{
    /** Do not visit any further nodes in the scenegraph. */
    STOP,
    /** Visit any further siblings, but not the children of the current node. */
    SKIP_CHILDREN,
    /** Visit children of the node first, then continue with siblings. */
    ALL,
};

/**
 * Describes the current state of a node.
 */
enum class node_flags : int
{
    /**
     * If set, the node should be ignored by visitors and any plugins iterating
     * over the scenegraph. Such nodes (and their children) do not wish to receive
     * pointer, keyboard, etc. events and do not wish to be displayed.
     *
     * Note that plugins might still force those nodes to receive input and be
     * rendered by calling the corresponding methods directly.
     */
    DISABLED = (1 << 0),
};

/**
 * Used as a result of an intersection of the scenegraph with the user input.
 */
struct input_node_t
{
    nonstd::observer_ptr<node_t> node;

    // FIXME: In the future, this should be a separate interface, allowing
    // non-surface nodes to get user input as well.
    wf::surface_interface_t *surface;

    // The coordinates of the user input in surface-local coordinates.
    wf::pointf_t local_coords;
};

/**
 * The base class for all nodes in the scenegraph.
 */
class node_t : public std::enable_shared_from_this<node_t>
{
  public:
    virtual ~node_t();

    /**
     * Find the input node at the given position.
     */
    virtual std::optional<input_node_t> find_node_at(const wf::pointf_t& at) = 0;

    /**
     * First visit the node and then its children from front to back.
     * For consistency, the parent node's visit implementation should not visit
     * disabled nodes.
     */
    virtual iteration visit(visitor_t *visitor) = 0;

    /**
     * Get a textual representation of the node, used for debugging purposes.
     * For example, see wf::dump_scene().
     * The representation should therefore not contain any newline characters.
     */
    virtual std::string stringify() const;

    /**
     * Get the current flags of the node.
     */
    virtual int flags() const
    {
        return enabled_counter > 0 ?
               0 : (int)node_flags::DISABLED;
    }

    /**
     * Get the keyboard interaction interface of this node.
     * By default, a no-op.
     */
    virtual keyboard_interaction_t& keyboard_interaction()
    {
        static keyboard_interaction_t noop;
        return noop;
    }

    virtual pointer_interaction_t& pointer_interaction()
    {
        static pointer_interaction_t noop;
        return noop;
    }

    virtual touch_interaction_t& touch_interaction()
    {
        static touch_interaction_t noop;
        return noop;
    }

    /**
     * Structure nodes are special nodes which core usually creates when Wayfire
     * is started (e.g. layer and output nodes). These nodes should not be
     * reordered or removed from the scenegraph.
     */
    bool is_structure_node() const
    {
        return _is_structure;
    }

    /**
     * Get the parent of the current node in the scene graph.
     */
    inner_node_t *parent() const
    {
        return this->_parent;
    }

    /**
     * A helper function to get the status of the DISABLED flag.
     */
    inline bool is_disabled() const
    {
        return flags() & (int)node_flags::DISABLED;
    }

    /**
     * Increase or decrease the enabled counter. A non-positive counter causes
     * the DISABLED flag to be set.
     *
     * By default, a node is created with an enabled counter equal to 1.
     */
    void set_enabled(bool is_enabled);

    /**
     * The limit region of a node.
     * The limit region is described in the node's parent coordinate system and
     * tells the node that it should not process input outside of it nor render
     * outside. By default, nodes do not have a limit region set, which means
     * they are not limited in any way.
     */
    std::optional<wf::geometry_t> limit_region;

  public:
    node_t(const node_t&) = delete;
    node_t(node_t&&) = delete;
    node_t& operator =(const node_t&) = delete;
    node_t& operator =(node_t&&) = delete;

  protected:
    node_t(bool is_structure);
    bool _is_structure;
    int enabled_counter   = 1;
    inner_node_t *_parent = nullptr;
    friend class inner_node_t;

    // A helper function for implementations of find_node_at.
    inline bool test_point_in_limit(const wf::pointf_t& point)
    {
        return !limit_region || (*limit_region & point);
    }

    // A helper functions for stringify() implementations, serializes the flags()
    // to a string, e.g. node with KEYBOARD and USER_INPUT -> '(ku)'
    std::string stringify_flags() const;
};

/**
 * An inner node of the scenegraph tree with a list of children.
 */
class inner_node_t : public node_t
{
  public:
    inner_node_t(bool _is_structure);

    iteration visit(visitor_t *visitor) override;
    std::optional<input_node_t> find_node_at(const wf::pointf_t& at) override;
    std::string stringify() const override;

    /**
     * Obtain an immutable list of the node's children.
     * Use set_children_list() to modify the children.
     */
    const std::vector<node_ptr>& get_children() const
    {
        return children;
    }

  protected:
    /**
     * A list of children nodes sorted from top to bottom.
     *
     * Note on special `structure` nodes: These nodes are typically present in
     * the normal list of children, but also accessible via a specialized pointer
     * in their parent's class.
     */
    std::vector<std::shared_ptr<node_t>> children;

    void set_children_unchecked(std::vector<node_ptr> new_list);
};

/**
 * Inner nodes where plugins can add their own nodes and whose children can be
 * reordered freely. However, special care needs to be taken to avoid reordering
 * the special `structure` nodes.
 */
class floating_inner_node_t : public inner_node_t
{
  public:
    using inner_node_t::inner_node_t;

    /**
     * Exchange the list of children of this node.
     * A typical usage (for example, bringing a node to the top):
     * 1. list = get_children()
     * 2. list.erase(target_node)
     * 3. list.insert(list.begin(), target_node)
     * 4. set_children_list(list)
     *
     * The set_children_list function also performs checks on the structure
     * nodes present in the inner node. If they were changed, the change is
     * rejected and false is returned. In all other cases, the list of
     * children is updated, and each child's parent is set to this node.
     */
    bool set_children_list(std::vector<node_ptr> new_list);
};
using floating_inner_ptr = std::shared_ptr<floating_inner_node_t>;

/**
 * A Level 3 node which represents each output in each layer.
 */
class output_node_t final : public inner_node_t
{
  public:
    output_node_t();
    std::string stringify() const override;

    /**
     * A container for the static child nodes.
     * Static child nodes are always below the dynamic nodes of an output and
     * are usually not modified when the workspace on the output changes, so
     * things like backgrounds and panels are usually static.
     */
    std::shared_ptr<floating_inner_node_t> _static;

    /**
     * A container for the dynamic child nodes.
     * These nodes move together with the output's workspaces.
     * These nodes are most commonly views.
     */
    std::shared_ptr<floating_inner_node_t> dynamic;
};

/**
 * A list of all layers in the root node.
 */
enum class layer : size_t
{
    BACKGROUND = 0,
    BOTTOM     = 1,
    WORKSPACE  = 2,
    TOP        = 3,
    UNMANAGED  = 4,
    OVERLAY    = 5,
    // For compatibility with workspace-manager, to be removed
    DWIDGET    = 6,
    /** Not a real layer, but a placeholder for the number of layers. */
    ALL_LAYERS,
};

/**
 * The root (Level 1) node of the whole scenegraph.
 */
class root_node_t final : public inner_node_t
{
  public:
    root_node_t();
    virtual ~root_node_t();
    std::string stringify() const override;

    /**
     * An ordered list of all layers' nodes.
     */
    std::shared_ptr<floating_inner_node_t> layers[(size_t)layer::ALL_LAYERS];

    /**
     * Signal to the root node that some node's flags have changed.
     * Doing this will trigger a recomputation of the input and output state
     * and must be called every time such a change is made.
     *
     * FIXME: this API has the potential to become very slow as the scenegraph
     * grows. In the future, we need to add specialized functions which receive
     * a list of updated nodes and then applies partial updates.
     */
    void update();

    struct priv_t;
    std::unique_ptr<priv_t> priv;
};

class view_node_t;
/**
 * An interface for iterating over the scenegraph.
 */
class visitor_t
{
  public:
    visitor_t(const visitor_t&) = delete;
    visitor_t(visitor_t&&) = delete;
    visitor_t& operator =(const visitor_t&) = delete;
    visitor_t& operator =(visitor_t&&) = delete;
    visitor_t() = default;
    virtual ~visitor_t() = default;

    /** Visit an inner node with children. */
    virtual iteration inner_node(inner_node_t *node)
    {
        return iteration::ALL;
    }

    /** Visit a view node. */
    virtual iteration view_node(view_node_t *node) = 0;

    /** Visit a generic node whose type is neither inner nor view. */
    virtual iteration generic_node(node_t *node)
    {
        return iteration::SKIP_CHILDREN;
    }
};
}
}
