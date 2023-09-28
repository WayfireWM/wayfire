#pragma once

#include "wayfire/scene-input.hpp"
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
inline void remove_child(node_ptr child, uint32_t add_flags = 0)
{
    if (!child->parent())
    {
        return;
    }

    auto parent = dynamic_cast<floating_inner_node_t*>(child->parent());
    wf::dassert(parent, "Removing a child from a non-floating container!");

    auto children = parent->get_children();
    children.erase(std::remove(children.begin(), children.end(), child),
        children.end());
    parent->set_children_list(children);
    update(parent->shared_from_this(), update_flag::CHILDREN_LIST | add_flags);
}

inline void add_front(floating_inner_ptr parent, node_ptr child)
{
    auto children = parent->get_children();
    children.insert(children.begin(), child);
    parent->set_children_list(children);
    update(parent, update_flag::CHILDREN_LIST);
}

inline void readd_front(floating_inner_ptr parent, node_ptr child)
{
    remove_child(child);
    add_front(parent, child);
}

inline void add_back(floating_inner_ptr parent, node_ptr child)
{
    auto children = parent->get_children();
    children.push_back(child);
    parent->set_children_list(children);
    update(parent, update_flag::CHILDREN_LIST);
}

inline void readd_back(floating_inner_ptr parent, node_ptr child)
{
    remove_child(child);
    add_back(parent, child);
}

inline bool raise_to_front(node_ptr child)
{
    auto dyn_parent = dynamic_cast<floating_inner_node_t*>(child->parent());
    wf::dassert(dyn_parent, "Raise to front in a non-floating container!");

    auto children = dyn_parent->get_children();
    if (children.front() == child)
    {
        return false;
    }

    children.erase(std::remove(children.begin(), children.end(), child),
        children.end());
    children.insert(children.begin(), child);
    dyn_parent->set_children_list(children);
    update(dyn_parent->shared_from_this(), update_flag::CHILDREN_LIST);
    return true;
}
}
}
