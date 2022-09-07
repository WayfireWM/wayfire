#include <memory>
#include <wayfire/surface.hpp>

#include "surface-pointer-interaction.cpp"
#include "surface-touch-interaction.cpp"
#include "wayfire/geometry.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene.hpp"

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

wf::pointf_t wf::scene::surface_node_t::to_local(const wf::pointf_t& point)
{
    auto offset = this->si->get_offset();
    wf::pointf_t local = point;
    local.x -= offset.x;
    local.y -= offset.y;
    return local;
}

wf::pointf_t wf::scene::surface_node_t::to_global(const wf::pointf_t& point)
{
    auto offset = this->si->get_offset();
    wf::pointf_t local = point;
    local.x += offset.x;
    local.y += offset.y;
    return local;
}

std::optional<input_node_t> surface_node_t::find_node_at(
    const wf::pointf_t& at)
{
    if (si->accepts_input(std::round(at.x), std::round(at.y)))
    {
        wf::scene::input_node_t result;
        result.node    = this;
        result.surface = si;
        result.local_coords = at;
        return result;
    }

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
