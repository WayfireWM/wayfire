// A collection of small utility functions that plugins use.
// FIXME: consider splitting into multiple files as util functions accumulate.
#pragma once

#include "wayfire/scene.hpp"
#include "wayfire/view.hpp"
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
}
