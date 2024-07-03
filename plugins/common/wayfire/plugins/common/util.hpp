// A collection of small utility functions that plugins use.
// FIXME: consider splitting into multiple files as util functions accumulate.
#pragma once

#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/view.hpp"
#include "wayfire/output.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/view-transform.hpp"
#include <memory>
namespace wf
{
inline uint64_t get_focus_timestamp(wayfire_view view)
{
    const auto& node = view->get_surface_root_node();
    return node->keyboard_interaction().last_focus_timestamp;
}

template<class Transformer, class... TransformerArgs>
inline std::shared_ptr<Transformer> ensure_view_transformer(
    wayfire_view view, int z_order, TransformerArgs... args)
{
    auto trmanager   = view->get_transformed_node();
    auto transformer = trmanager->get_transformer<Transformer>();
    if (!transformer)
    {
        transformer = std::make_shared<Transformer>(args...);
        trmanager->add_transformer(transformer, z_order);
    }

    return transformer;
}

template<class Transformer, class... TransformerArgs>
inline std::shared_ptr<Transformer> ensure_named_transformer(
    wayfire_view view, int z_order, std::string name, TransformerArgs... args)
{
    auto trmanager   = view->get_transformed_node();
    auto transformer = trmanager->get_transformer<Transformer>(name);
    if (!transformer)
    {
        transformer = std::make_shared<Transformer>(args...);
        trmanager->add_transformer(transformer, z_order, name);
    }

    return transformer;
}

template<class Transformer = wf::scene::floating_inner_node_t>
inline wf::geometry_t view_bounding_box_up_to(wayfire_view view,
    std::string name = typeid(Transformer).name())
{
    auto transformer = view->get_transformed_node()->get_transformer(name);
    if (transformer)
    {
        return transformer->get_children_bounding_box();
    } else
    {
        return view->get_transformed_node()->get_bounding_box();
    }
}

/**
 * Find the topmost view on the given coordinates on the given output, bypassing any overlays / input grabs.
 */
inline wayfire_toplevel_view find_output_view_at(wf::output_t *output, const wf::pointf_t& coords)
{
    for (auto& output_node : wf::collect_output_nodes(wf::get_core().scene(), output))
    {
        auto as_output = std::dynamic_pointer_cast<scene::output_node_t>(output_node);
        if (!as_output || (as_output->get_output() != output) || !as_output->is_enabled())
        {
            continue;
        }

        // We start the search directly from the output node's children. This is because the output nodes
        // usually reject all queries outside of their current visible geometry, but we want to be able to
        // query views from all workspaces, not just the current (and the only visible) one.
        for (auto& ch : output_node->get_children())
        {
            if (!ch->is_enabled())
            {
                continue;
            }

            auto isec = ch->find_node_at(coords);
            auto node = isec ? isec->node.get() : nullptr;

            if (auto view = wf::toplevel_cast(wf::node_to_view(node)))
            {
                if (wf::find_topmost_parent(view)->get_wset() == output->wset())
                {
                    return view;
                }
            }

            if (node)
            {
                return nullptr;
            }
        }
    }

    return nullptr;
}
}
