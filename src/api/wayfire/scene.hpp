#pragma once

#include <optional>
#include <memory>
#include <vector>
#include <wayfire/geometry.hpp>
#include <wayfire/region.hpp>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/scene-input.hpp>
#include <wayfire/signal-provider.hpp>

namespace wf
{
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
 * Level 4 and beyond: These levels typically contain views and group of views,
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
 *   0.8.0. To emulate a grab, create an input-only scene node and place it above
 *   every other node. Thus it will always be selected for input on the output it
 *   is visible on.
 *
 * - Always-on-top views are simply nodes which are placed above the workspace
 *   set of each output.
 *
 * Regarding coordinate systems: each node possesses a coordinate system. Some
 * nodes (for example, nodes which simply group other nodes together) share the
 * coordinate system of their parent node. Other nodes (for example transformers)
 * are responsible for converting between the coordinate system of their children
 * and the coordinate system of their parent.
 */
namespace scene
{
class node_t;
using node_ptr = std::shared_ptr<node_t>;
using node_weak_ptr = std::weak_ptr<node_t>;

class render_instance_t;
using render_instance_uptr = std::unique_ptr<render_instance_t>;
using damage_callback = std::function<void (const wf::region_t&)>;

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
    DISABLED  = (1 << 0),
    /**
     * If set, the node indicates that it wishes to receive raw input events, that is, it may receive
     * unmatched pointer press/release events, unmatched touch up/down events, etc.
     */
    RAW_INPUT = (1 << 1),
};

using node_flags_bitmask_t = uint64_t;

/**
 * A list of bitmask flags which indicate what parts of the node state have
 * changed. The information is useful when updating the scenegraph's state
 * with wf::scene::update().
 */
namespace update_flag
{
enum update_flag
{
    /**
     * The list of the node's children changed.
     */
    CHILDREN_LIST = (1 << 0),
    /**
     * The node's enabled or disabled state changed.
     */
    ENABLED       = (1 << 1),
    /**
     * The node's input state changed, that is, the result of find_node_at()
     * may have changed. Typically, this is triggered when a surface is mapped,
     * unmapped or moved.
     */
    INPUT_STATE   = (1 << 2),
    /**
     * The node's geometry changed. Changes include not just the bounding box
     * of the view, but also things like opaque regions.
     */
    GEOMETRY      = (1 << 3),
    /**
     * A keyboard refocus might be necessary (for example, node removed, keyboard input state changed, etc.).
     */
    REFOCUS       = (1 << 4),
    /**
     * The update concerns a disabled node (which was disabled before and after the operation, so many
     * updates (for example regenerating render instances) are not necessary).
     */
    MASKED        = (1 << 5),
};
}

/**
 * Used as a result of an intersection of the scenegraph with the user input.
 */
struct input_node_t
{
    nonstd::observer_ptr<node_t> node;

    // The coordinates of the user input in surface-local coordinates.
    wf::pointf_t local_coords;
};

/**
 * The base class for all nodes in the scenegraph.
 */
class node_t : public std::enable_shared_from_this<node_t>,
    public wf::signal::provider_t
{
  public:
    /**
     * Create a new no-op node.
     * Plugins very rarely need this, instead, subclasses of node_t should be
     * instantiated.
     */
    node_t(bool is_structure);
    virtual ~node_t();

    /**
     * Find the input node at the given position.
     * By default, the node will try to pass input to its children.
     *
     * @param at The point at which the query is made. It is always in the node's
     *   coordinate system (e.g. resulting from the parent's to_local() function).
     */
    virtual std::optional<input_node_t> find_node_at(const wf::pointf_t& at);

    /**
     * Figure out which node should receive keyboard focus on the given output.
     *
     * Typically, the focus is set directly via core::set_active_node(). However,
     * in some cases we need to re-elect a node to focus (for example if the
     * focused node is destroyed). In these cases, the keyboard_refocus() method
     * on a node is called. It should return the desired focus node.
     *
     * By default, a node tries to focus one of its focusable children with the
     * highest focus_importance. In case of a tie, the node with the highest
     * last_focus_timestamp is selected.
     */
    virtual wf::keyboard_focus_node_t keyboard_refocus(wf::output_t *output);

    /**
     * Convert a point from the coordinate system the node resides in, to the
     * coordinate system of its children.
     *
     * By default, the node's children share the coordinate system of their parent,
     * that is, `to_local(x) == x`.
     */
    virtual wf::pointf_t to_local(const wf::pointf_t& point);

    /**
     * Convert a point from the coordinate system of the node's children to
     * the coordinate system the node resides in. Typically, this is the inverse
     * operation of to_local, e.g. `to_global(to_local(x)) == x`.
     *
     * By default, the node's children share the coordinate system of their parent,
     * that is, `to_global(x) == x`.
     */
    virtual wf::pointf_t to_global(const wf::pointf_t& point);

    /**
     * Get a textual representation of the node, used for debugging purposes.
     * For example, see wf::dump_scene().
     * The representation should therefore not contain any newline characters.
     */
    virtual std::string stringify() const;

    /**
     * Get the current flags of the node.
     */
    virtual node_flags_bitmask_t flags() const
    {
        return enabled_counter > 0 ? 0 : (int)node_flags::DISABLED;
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
     * Generate render instances for this node and its children.
     * See the @render_instance_t interface for more details.
     *
     * The default implementation just generates render instances from its
     * children.
     *
     * @param instances A vector of render instances to add to. The instances
     *   are sorted from the foremost (or topmost) to the last (bottom-most).
     * @param push_damage A callback used to report damage on the new render
     *   instance.
     * @param output An optional parameter describing which output the render
     *   instances will be shown on. It can be used to avoid generating instances
     *   on outputs where the node should not be shown. However, this should be
     *   conservatively approximated - it is fine to generate more render
     *   instances than necessary, but not less.
     */
    virtual void gen_render_instances(
        std::vector<render_instance_uptr>& instances,
        damage_callback push_damage,
        wf::output_t *output = nullptr);

    /**
     * Get a bounding box of the node in the node's parent coordinate system.
     *
     * The bounding box is a rectangular region in which the node and its
     * children are fully contained.
     *
     * The default implementation ignores the node itself and simply returns
     * the same result as @get_children_bounding_box. Nodes may override this
     * function if they want to apply a transform to their children, or if the
     * nodes themselves have visible elements that should be included in the
     * bounding box.
     */
    virtual wf::geometry_t get_bounding_box();

    /**
     * Get the bounding box of the node's children, in the coordinate system of
     * the node.
     *
     * In contrast to @get_bounding_box, this does not include the node itself,
     * and does not apply any transformations which may be implemented by the
     * node. It is simply the bounding box of the bounding boxes of the children
     * as reported by their get_bounding_box() method.
     */
    wf::geometry_t get_children_bounding_box();

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
    node_t *parent() const
    {
        return _parent;
    }

    /**
     * A helper function to get the status of the DISABLED flag.
     */
    inline bool is_enabled() const
    {
        return !(flags() & (int)node_flags::DISABLED);
    }

    /**
     * A helper function to get the status of the RAW_INPUT flag.
     */
    inline bool wants_raw_input() const
    {
        return (flags() & (int)node_flags::RAW_INPUT);
    }

    /**
     * Increase or decrease the enabled counter. A non-positive counter causes
     * the DISABLED flag to be set.
     *
     * By default, a node is created with an enabled counter equal to 1.
     */
    void set_enabled(bool is_enabled);

    /**
     * Obtain an immutable list of the node's children.
     * Use set_children_list() of floating_inner_node_t to modify the children,
     * if the node supports that.
     */
    const std::vector<node_ptr>& get_children() const
    {
        return children;
    }

    /**
     * When a scenegraph change happens, core or the plugin which modifies the scenegraph is supposed to call
     * the @scene::update() function defined below, so that the scene graph can be updated properly, render
     * instances regenerated, etc.
     *
     * However, in many cases a full update is not necessary. For example, when subsurfaces are being
     * reordered, locally regenerating the render instances within the view render instances is enough.
     * For such cases, nodes can override this function and change the information which is propagated for
     * the update to their parent nodes. In the above example of subsurface reordering, the subsurface root
     * will update all of its render instances manually and not propagate CHILDREN_LIST updates to its parent.
     */
    virtual uint32_t optimize_update(uint32_t update_flags);

  public:
    node_t(const node_t&) = delete;
    node_t(node_t&&) = delete;
    node_t& operator =(const node_t&) = delete;
    node_t& operator =(node_t&&) = delete;

  protected:
    bool _is_structure;
    int enabled_counter = 1;
    node_t *_parent     = nullptr;
    friend class surface_root_node_t;
    friend class floating_inner_node_t;

    // A helper functions for stringify() implementations, serializes the flags()
    // to a string, e.g. node with KEYBOARD and USER_INPUT -> '(ku)'
    std::string stringify_flags() const;

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
class floating_inner_node_t : public node_t
{
  public:
    using node_t::node_t;
    ~floating_inner_node_t();

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
 *
 * Each output's children reside in a coordinate system offsetted by the output's
 * position in the output layout, e.g. each output has a position 0,0 in its
 * coordinate system.
 */
class output_node_t : public floating_inner_node_t
{
  public:
    output_node_t(wf::output_t *output);
    std::string stringify() const override;

    wf::pointf_t to_local(const wf::pointf_t& point) override;
    wf::pointf_t to_global(const wf::pointf_t& point) override;

    /**
     * The output's render instance simply adjusts damage, rendering, etc. to
     * account for the output's position in the output layout.
     */
    void gen_render_instances(
        std::vector<render_instance_uptr>& instances,
        damage_callback push_damage,
        wf::output_t *output) override;

    wf::geometry_t get_bounding_box() override;
    std::optional<input_node_t> find_node_at(const wf::pointf_t& at) override;

    /**
     * Get the output this node is responsible for.
     */
    wf::output_t *get_output() const
    {
        return output;
    }

    /**
     * The limit region of an output.
     * It defines the region of the output layout that this output occupies.
     * The output will not render anything outside of its limit region, and will
     * not find any intersections via find_node_at.
     */
    std::optional<wf::geometry_t> limit_region;

  private:
    wf::output_t *output;
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
    LOCK       = 6,
    // For compatibility with workspace-manager, to be removed
    DWIDGET    = 7,
    /** Not a real layer, but a placeholder for the number of layers. */
    ALL_LAYERS,
};

/**
 * A signal that the root node has been updated.
 *
 * on: scenegraph's root
 * when: Emitted when an update sequence finishes at the scenegraph's root.
 */
struct root_node_update_signal
{
    uint32_t flags;
};

/**
 * The root (Level 1) node of the whole scenegraph.
 */
class root_node_t final : public floating_inner_node_t
{
  public:
    root_node_t();
    virtual ~root_node_t();
    std::string stringify() const override;

    /**
     * An ordered list of all layers' nodes.
     */
    std::shared_ptr<floating_inner_node_t> layers[(size_t)layer::ALL_LAYERS];

    struct priv_t;
    std::unique_ptr<priv_t> priv;
};

/**
 * Increase or decrease the node's enabled counter (node_t::set_enabled()) and
 * also trigger a scenegraph update if necessary.
 */
void set_node_enabled(wf::scene::node_ptr node, bool enabled);

/**
 * Trigger an update of the scenegraph's state.
 *
 * When any state of the node changes, this function should be called with a
 * bitmask list of flags that indicates which parts of the node's state changed.
 *
 * After updating the concrete node's state, the change is propagated to parent
 * nodes all the way up to the scenegraph's root.
 *
 * @param changed_node The node whose state changed.
 * @param flags A bit mask consisting of flags defined in the @update_flag enum.
 */
void update(node_ptr changed_node, uint32_t flags);
}
} // namespace wf
