#include "view-keyboard-interaction.cpp"
#include <wayfire/debug.hpp>
#include <wayfire/output.hpp>
#include "../core/core-impl.hpp"
#include "../core/seat/input-manager.hpp"

wf::scene::view_node_t::view_node_t(wayfire_view _view) :
    floating_inner_node_t(false), view(_view)
{
    this->kb_interaction = std::make_unique<view_keyboard_interaction_t>(view);
}

wf::scene::view_node_t::view_node_t() : floating_inner_node_t(false)
{}

std::string wf::scene::view_node_t::stringify() const
{
    std::ostringstream out;
    out << this->view;
    return out.str() + " " + stringify_flags();
}

wf::keyboard_interaction_t& wf::scene::view_node_t::keyboard_interaction()
{
    return *kb_interaction;
}

std::optional<wf::scene::input_node_t> wf::scene::view_node_t::find_node_at(
    const wf::pointf_t& at)
{
    if (!test_point_in_limit(at))
    {
        return {};
    }

    if (view->minimized || !view->is_visible() ||
        !wf::get_core_impl().input->can_focus_surface(view.get()))
    {
        return {};
    }

    wf::pointf_t local_coordinates = at;

    // First, translate to the view's output
    if (view->get_output())
    {
        auto offset = wf::origin(view->get_output()->get_layout_geometry());
        local_coordinates.x -= offset.x;
        local_coordinates.y -= offset.y;
    }

    input_node_t result;
    result.surface =
        view->map_input_coordinates(local_coordinates, result.local_coords);

    if (result.surface)
    {
        result.node = result.surface->get_content_node().get();
        return result;
    }

    // Empty std::optional => No intersection
    return {};
}
