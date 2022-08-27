#pragma once

#include <wayfire/scene.hpp>
#include <wayfire/debug.hpp>

// This header contains implementations of simple scenegraph related functionality
// used in many places throughout the codebase.

namespace wf
{
namespace scene
{
/**
 * Remove a child node from a parent node and update the parent.
 */
inline void remove_child(floating_inner_ptr parent, node_ptr child)
{
    auto children = parent->get_children();
    children.erase(std::remove(children.begin(), children.end(), child),
        children.end());
    parent->set_children_list(children);
}

inline void add_front(floating_inner_ptr parent, node_ptr child)
{
    auto children = parent->get_children();
    children.insert(children.begin(), child);
    parent->set_children_list(children);
}

inline void add_back(floating_inner_ptr parent, node_ptr child)
{
    auto children = parent->get_children();
    children.push_back(child);
    parent->set_children_list(children);
}

inline void raise_to_front(node_ptr child)
{
    auto dyn_parent = dynamic_cast<floating_inner_node_t*>(child->parent());
    wf::dassert(dyn_parent, "Raise to front in a non-floating container!");

    auto children = dyn_parent->get_children();
    children.erase(std::remove(children.begin(), children.end(), child),
        children.end());
    children.insert(children.begin(), child);
    dyn_parent->set_children_list(children);
}
}
}
