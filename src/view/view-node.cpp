#include "view-keyboard-interaction.cpp"
#include <wayfire/debug.hpp>
#include <wayfire/output.hpp>
#include "../core/core-impl.hpp"
#include "../core/seat/input-manager.hpp"
#include "wayfire/scene.hpp"

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

wf::pointf_t wf::scene::view_node_t::to_local(const wf::pointf_t& point)
{
    auto local = view->global_to_local_point(point, nullptr);
    return local;
}

wf::pointf_t wf::scene::view_node_t::to_global(const wf::pointf_t& point)
{
    auto local = point;
    local.x += view->get_output_geometry().x;
    local.y += view->get_output_geometry().y;
    return view->transform_point(local);
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

    return floating_inner_node_t::find_node_at(at);
}
