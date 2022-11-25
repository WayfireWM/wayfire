#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>

inline wayfire_view scale_find_view_at(wf::pointf_t at, wf::output_t *output)
{
    auto offset = wf::origin(output->get_layout_geometry());
    at.x -= offset.x;
    at.y -= offset.y;

    auto node = output->get_wset()->find_node_at(at);
    if (node && node->surface)
    {
        auto view = dynamic_cast<wf::view_interface_t*>(
            node->surface->get_main_surface());
        return {view};
    }

    return nullptr;
}
