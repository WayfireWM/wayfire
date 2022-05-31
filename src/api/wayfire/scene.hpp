#pragma once

#include <optional>
#include <memory>
#include <vector>
#include <wayfire/geometry.hpp>
#include <wayfire/region.hpp>

namespace wf
{
class surface_interface_t;
/**
 * Contains definitions of the common elements used in Wayfire's scenegraph.
 *
 * The scenegraph is a complete representation of the current rendering and input
 * state of Wayfire. The basic nodes forms a tree where every node is responsible
 * for managing its children's state. More advanced nodes like workspace stream
 * nodes may also add additional edges which make the scenegraph a directed
 * acyclic graph.
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
 */
namespace scene
{
class node_t;
using node_ptr = std::shared_ptr<node_t>;

/**
 * Used as a result of an intersection of the scenegraph with the user input.
 */
struct input_node_t
{
    const node_ptr& node;
    // FIXME: In the future, this should be a separate interface, allowing
    // non-surface nodes to get user input as well.
    wf::surface_interface_t *surface;
};

/**
 * The base class for all nodes in the scenegraph.
 */
class node_t
{
  public:
    virtual ~node_t();

    /**
     * Find the input node at the given position.
     */
    virtual const node_ptr& find_node_at(const wf::pointf_t& at) = 0;

  public:
    node_t(const node_t&) = delete;
    node_t(node_t&&) = delete;
    node_t& operator =(const node_t&) = delete;
    node_t& operator =(node_t&&) = delete;
};

/**
 * A node (at any level) which has several children nodes and delegates
 * work to them.
 */
class base_container_t : public node_t
{
    virtual const node_ptr& find_node_at(const wf::pointf_t& at) = 0;

  protected:
    std::vector<std::shared_ptr<node_t>> children;
};

/**
 * The root (Level 1) node of the whole scenegraph.
 */
class root_node_t : public node_t
{};

/**
 * A node which represents a layer (Level 2) in the scenegraph.
 */
class layer_node_t : public node_t
{};
}
}
