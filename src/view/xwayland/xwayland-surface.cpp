#include "xwayland-surface.hpp"

void wf::xw::xwayland_surface_node_t::set_scale(float scale)
{
    if (scale != this->current_scale)
    {
        wf::scene::damage_node(this, this->get_bounding_box());
        const float rescale = this->current_scale / scale;

        this->current_state.size.width  = int(this->current_state.size.width * rescale);
        this->current_state.size.height = int(this->current_state.size.height * rescale);
        this->current_state.accumulated_damage *= rescale;
        this->current_state.opaque_region *= rescale;
        if (this->current_state.src_viewport.has_value())
        {
            wlr_fbox& box = this->current_state.src_viewport.value();
            box = box * rescale;
        }

        this->current_scale = scale;
        wf::scene::damage_node(this, this->get_bounding_box());
    }
}

std::string wf::xw::xwayland_surface_node_t::stringify() const
{
    std::ostringstream name;
    name << "xwayland(scale=" << current_scale << ") " << wlr_surface_node_t::stringify();
    return name.str();
}

wf::pointf_t wf::xw::xwayland_surface_node_t::to_local(const wf::pointf_t& point)
{
    return {point.x * current_scale, point.y * current_scale};
}

wf::pointf_t wf::xw::xwayland_surface_node_t::to_global(const wf::pointf_t& point)
{
    return {point.x / current_scale, point.y / current_scale};
}

void wf::xw::xwayland_surface_node_t::apply_state(scene::surface_state_t&& state)
{
    state.size = wf::dimensions_t{
        int(state.size.width / current_scale),
        int(state.size.height / current_scale)
    };
    state.accumulated_damage *= (1.0 / current_scale);
    state.opaque_region *= (1.0 / current_scale);
    if (state.src_viewport.has_value())
    {
        wlr_fbox& box = state.src_viewport.value();
        box.x     /= current_scale;
        box.y     /= current_scale;
        box.width /= current_scale;
        box.height /= current_scale;
    }

    wlr_surface_node_t::apply_state(std::move(state));
}
