#include <wayfire/surface.hpp>

#include "surface-pointer-interaction.cpp"

namespace wf
{
namespace scene
{
surface_node_t::surface_node_t(wf::surface_interface_t *si) : node_t(false)
{
    this->si = si;
    this->ptr_interaction = std::make_unique<surface_pointer_interaction_t>(si);
}

std::optional<input_node_t> surface_node_t::find_node_at(
    const wf::pointf_t& at)
{
    // FIXME: The view node is the one which computes this, in the future it
    // should just apply the transformers and delegate implementation to the
    // actual surfaces.
    return {};
}

wf::scene::iteration wf::scene::surface_node_t::visit(visitor_t *visitor)
{
    return visitor->generic_node(this);
}

std::string wf::scene::surface_node_t::stringify() const
{
    return "surface " + stringify_flags();
}

wf::pointer_interaction_t& wf::scene::surface_node_t::pointer_interaction()
{
    return *ptr_interaction;
}
}
}
