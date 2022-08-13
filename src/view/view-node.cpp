#include "view-pointer-interaction.cpp"
#include "view-keyboard-interaction.cpp"
#include <wayfire/debug.hpp>

wf::scene::view_node_t::view_node_t(wayfire_view _view) : node_t(false), view(_view)
{
    this->kb_interaction  = std::make_unique<view_keyboard_interaction_t>(view);
    this->ptr_interaction = std::make_unique<view_pointer_interaction_t>(view);
}

wf::scene::iteration wf::scene::view_node_t::visit(visitor_t *visitor)
{
    visitor->view_node(this);
    return iteration::SKIP_CHILDREN;
}

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

wf::pointer_interaction_t& wf::scene::view_node_t::pointer_interaction()
{
    return *ptr_interaction;
}

std::optional<wf::scene::input_node_t> wf::scene::view_node_t::find_node_at(
    const wf::pointf_t& at)
{
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
        result.node = this;
        return result;
    }

    // Empty std::optional => No intersection
    return {};
}
