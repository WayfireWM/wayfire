#include <memory>
#include <wayfire/surface.hpp>

#include "surface-pointer-interaction.cpp"
#include "surface-touch-interaction.cpp"
#include "wayfire/geometry.hpp"
#include "wayfire/scene-input.hpp"

namespace wf
{
namespace scene
{
surface_node_t::surface_node_t(wf::surface_interface_t *si) : node_t(false)
{
    this->si = si;
    this->ptr_interaction = std::make_unique<surface_pointer_interaction_t>(si);
    this->tch_interaction = std::make_unique<surface_touch_interaction_t>(si);
}

std::optional<input_node_t> surface_node_t::find_node_at(
    const wf::pointf_t& at)
{
    // FIXME: The view node is the one which computes this, in the future it
    // should just apply the transformers and delegate implementation to the
    // actual surfaces.
    return {};
}

std::string wf::scene::surface_node_t::stringify() const
{
    return "surface " + stringify_flags();
}

wf::pointer_interaction_t& wf::scene::surface_node_t::pointer_interaction()
{
    return *ptr_interaction;
}

wf::touch_interaction_t& wf::scene::surface_node_t::touch_interaction()
{
    return *tch_interaction;
}
}
}
