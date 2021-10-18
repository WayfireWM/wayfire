#include <algorithm>
#include <map>
#include <wayfire/util/log.hpp>
#include "surface-impl.hpp"
#include "subsurface.hpp"
#include "wayfire/opengl.hpp"
#include "../core/core-impl.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/output.hpp"
#include <wayfire/util/log.hpp>
#include "wayfire/render-manager.hpp"
#include "wayfire/signal-definitions.hpp"

#include "../core/seat/pointer.hpp"
#include "../core/seat/seat.hpp"

/****************************
* surface_interface_t functions
****************************/
wf::surface_interface_t::surface_interface_t()
{
    this->priv = std::make_unique<impl>();
    this->priv->parent_surface = nullptr;
}

void wf::surface_interface_t::add_subsurface(
    std::unique_ptr<surface_interface_t> subsurface, bool is_below_parent)
{
    subsurface->priv->parent_surface = this;
    auto& container = is_below_parent ?
        priv->surface_children_below : priv->surface_children_above;

    wf::subsurface_added_signal ev;
    ev.main_surface = this;
    ev.subsurface   = {subsurface};

    container.insert(container.begin(), std::move(subsurface));
    this->emit_signal("subsurface-added", &ev);
}

std::unique_ptr<wf::surface_interface_t> wf::surface_interface_t::remove_subsurface(
    nonstd::observer_ptr<surface_interface_t> subsurface)
{
    auto remove_from = [=] (auto& container)
    {
        auto it = std::find_if(container.begin(), container.end(),
            [=] (const auto& ptr) { return ptr.get() == subsurface.get(); });

        std::unique_ptr<surface_interface_t> ret = nullptr;
        if (it != container.end())
        {
            ret = std::move(*it);
            container.erase(it);
        }

        return ret;
    };

    wf::subsurface_removed_signal ev;
    ev.main_surface = this;
    ev.subsurface   = subsurface;
    this->emit_signal("subsurface-removed", &ev);

    if (auto surf = remove_from(priv->surface_children_above))
    {
        return surf;
    }

    return remove_from(priv->surface_children_below);
}

wf::surface_interface_t::~surface_interface_t()
{}

wf::surface_interface_t*wf::surface_interface_t::get_parent()
{
    if (priv->parent_surface)
    {
        return priv->parent_surface;
    }

    return nullptr;
}

std::vector<wf::surface_iterator_t> wf::surface_interface_t::enumerate_surfaces(
    bool mapped_only)
{
    std::vector<wf::surface_iterator_t> result;
    result.reserve(priv->last_cnt_surfaces);
    auto add_surfaces_recursive = [&] (surface_interface_t *child)
    {
        auto child_surfaces = child->enumerate_surfaces(mapped_only);
        std::transform(child_surfaces.begin(), child_surfaces.end(),
            std::back_inserter(result), [=] (wf::surface_iterator_t child)
        {
            child.position =
                child.position + child.surface->output().get_offset();
            return child;
        });
    };

    for (auto& child : priv->surface_children_above)
    {
        add_surfaces_recursive(child.get());
    }

    if (!mapped_only || is_mapped())
    {
        result.push_back({this, wf::point_t{0, 0}});
    }

    for (auto& child : priv->surface_children_below)
    {
        add_surfaces_recursive(child.get());
    }

    priv->last_cnt_surfaces = result.size();
    return result;
}

/* Static method */
int wf::surface_interface_t::impl::active_shrink_constraint = 0;

void wf::surface_interface_t::set_opaque_shrink_constraint(
    std::string constraint_name, int value)
{
    static std::map<std::string, int> shrink_constraints;

    shrink_constraints[constraint_name] = value;

    impl::active_shrink_constraint = 0;
    for (auto& constr : shrink_constraints)
    {
        impl::active_shrink_constraint =
            std::max(impl::active_shrink_constraint, constr.second);
    }
}

int wf::surface_interface_t::get_active_shrink_constraint()
{
    return impl::active_shrink_constraint;
}

wlr_surface*wf::surface_interface_t::get_wlr_surface()
{
    return nullptr;
}

/****************************
* surface_interface_t functions for surfaces which are
* backed by a wlr_surface
****************************/
wf::surface_damage_signal::surface_damage_signal(const wf::region_t& damage) :
    damage(damage)
{}

void wf::surface_interface_t::emit_damage(wf::region_t damage)
{
    wf::point_t toplevel_offset = {0, 0};
    auto surface = this;

    while (surface->get_parent())
    {
        toplevel_offset = toplevel_offset + surface->output().get_offset();
        surface = surface->get_parent();
    }

    damage = damage + toplevel_offset;
    surface_damage_signal ds(damage);
    surface->emit_signal("damage", &ds);
}

void wf::surface_interface_t::clear_subsurfaces()
{
    subsurface_removed_signal ev;
    ev.main_surface = this;
    const auto& finish_subsurfaces = [&] (auto& container)
    {
        for (auto& surface : container)
        {
            ev.subsurface = {surface};
            this->emit_signal("subsurface-removed", &ev);
        }

        container.clear();
    };

    finish_subsurfaces(priv->surface_children_above);
    finish_subsurfaces(priv->surface_children_below);
}

wf::wlr_surface_base_t::wlr_surface_base_t(wlr_surface *surf)
{
    this->surface = surf;
    surf->data    = this;

    handle_new_subsurface = [&] (void *data)
    {
        auto sub = static_cast<wlr_subsurface*>(data);
        if (sub->data)
        {
            LOGE("Creating the same subsurface twice!");

            return;
        }

        // parent isn't mapped yet
        if (!sub->parent->data)
        {
            return;
        }

        auto subsurface = std::make_unique<subsurface_implementation_t>(sub);
        nonstd::observer_ptr<subsurface_implementation_t> ptr{subsurface};
        this->add_subsurface(std::move(subsurface), false);

        // Add outputs from the parent surface
        wlr_surface_output *output;
        wl_list_for_each(output, &sub->parent->current_outputs, link)
        {
            auto wo = wf::get_core().output_layout->find_output(output->output);
            ptr->output().set_visible_on_output(wo, true);
        }

        if (sub->mapped)
        {
            ptr->map();
        }
    };

    on_new_subsurface.set_callback(handle_new_subsurface);
    on_commit.set_callback([&] (void*) { this->handle_commit(); });
}

wf::wlr_surface_base_t::~wlr_surface_base_t()
{
    this->surface->data = NULL;
}

void wf::emit_map_state_change(wf::surface_interface_t *surface)
{
    std::string state =
        surface->is_mapped() ? "surface-mapped" : "surface-unmapped";

    surface_map_state_changed_signal data;
    data.surface = surface;
    wf::get_core().emit_signal(state, &data);
}

void wf::wlr_surface_base_t::map()
{
    assert(!this->mapped);
    this->mapped = true;

    emit_damage(wf::construct_box({0, 0}, output().get_size()));
    on_new_subsurface.connect(&surface->events.new_subsurface);
    on_commit.connect(&surface->events.commit);

    /* Handle subsurfaces which were created before this surface was mapped */
    wlr_subsurface *sub;
    wl_list_for_each(sub, &surface->current.subsurfaces_below, current.link)
    handle_new_subsurface(sub);
    wl_list_for_each(sub, &surface->current.subsurfaces_above, current.link)
    handle_new_subsurface(sub);

    emit_map_state_change(this);
}

void wf::wlr_surface_base_t::unmap()
{
    assert(this->mapped);

    apply_surface_damage();
    wf::region_t dmg{{.x = 0, .y = 0,
        .width = get_size().width, .height = get_size().height}};
    emit_damage(dmg);

    this->mapped = false;
    emit_map_state_change(this);

    on_new_subsurface.disconnect();
    on_destroy.disconnect();
    on_commit.disconnect();

    // Clear all subsurfaces we have.
    // This might remove subsurfaces that will be re-created again on map.
    this->clear_subsurfaces();
}

void wf::wlr_surface_base_t::apply_surface_damage()
{
    if (!is_mapped())
    {
        return;
    }

    wf::region_t dmg;
    wlr_surface_get_effective_damage(surface, dmg.to_pixman());

    // If the surface is scaled or doesn't match the scale of an output it is
    // on, then when we render it, it may get rendered over a fractional amount
    // of pixels. This means that we need to damage the neighboring pixels as
    // well, hence we expand the damage by 1 pixel.
    bool needs_expansion = (surface->current.scale != 1);
    for (auto [wo, cnt] : visibility)
    {
        needs_expansion |= (wo->handle->scale != surface->current.scale);
    }

    if (needs_expansion)
    {
        dmg.expand_edges(1);
    }

    emit_damage(dmg);
}

void wf::wlr_surface_base_t::handle_commit()
{
    apply_surface_damage();
    for (auto& [wo, cnt] : this->visibility)
    {
        wo->render->schedule_redraw();
    }
}

bool wf::wlr_surface_base_t::accepts_input(wf::pointf_t at)
{
    if (!surface)
    {
        return false;
    }

    return wlr_surface_point_accepts_input(surface, at.x, at.y);
}

std::optional<wf::region_t> wf::wlr_surface_base_t::handle_pointer_enter(
    wf::pointf_t at, bool reenter)
{
    if (surface)
    {
        auto seat = wf::get_core().get_current_seat();
        wlr_seat_pointer_notify_enter(seat, surface, at.x, at.y);

        auto constraint = wlr_pointer_constraints_v1_constraint_for_surface(
            wf::get_core().protocols.pointer_constraints, surface, seat);
        if (constraint == nullptr)
        {
            return {};
        }

        if (current_constraint != constraint)
        {
            if (current_constraint)
            {
                wlr_pointer_constraint_v1_send_deactivated(current_constraint);
            }

            wlr_pointer_constraint_v1_send_activated(constraint);
            on_current_constraint_destroy.set_callback([=] (void*)
            {
                this->current_constraint = NULL;
            });
            on_current_constraint_destroy.disconnect();
            on_current_constraint_destroy.connect(&constraint->events.destroy);

            current_constraint = constraint;
        }

        wf::region_t region;
        if (constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED)
        {
            region = wf::region_t{&constraint->region};
        }

        return region;
    }

    return {};
}

void wf::wlr_surface_base_t::handle_pointer_leave()
{
    if (current_constraint)
    {
        wlr_pointer_constraint_v1_send_deactivated(current_constraint);
        current_constraint = NULL;
    }
}

void wf::wlr_surface_base_t::handle_pointer_button(uint32_t time_ms, uint32_t button,
    wlr_button_state state)
{
    auto seat = wf::get_core().get_current_seat();
    wlr_seat_pointer_notify_button(seat, time_ms, button, state);
}

void wf::wlr_surface_base_t::handle_pointer_motion(uint32_t time_ms, wf::pointf_t at)
{
    auto seat = wf::get_core().get_current_seat();
    wlr_seat_pointer_notify_motion(seat, time_ms, at.x, at.y);
}

void wf::wlr_surface_base_t::handle_pointer_axis(uint32_t time_ms,
    wlr_axis_orientation orientation, double delta,
    int32_t delta_discrete, wlr_axis_source source)
{
    auto seat = wf::get_core().get_current_seat();
    wlr_seat_pointer_notify_axis(seat, time_ms, orientation,
        delta, delta_discrete, source);
}

void wf::wlr_surface_base_t::handle_touch_down(
    uint32_t time_ms, int32_t id, wf::pointf_t at)
{
    auto seat = wf::get_core().get_current_seat();
    if (surface)
    {
        auto touch_point = wlr_seat_touch_get_point(seat, id);
        if (!touch_point)
        {
            wlr_seat_touch_notify_down(seat, surface, time_ms, id, at.x, at.y);
        } else if (surface != touch_point->focus_surface)
        {
            wlr_seat_touch_point_focus(seat, surface, time_ms, id, at.x, at.y);
        }
    }
}

void wf::wlr_surface_base_t::handle_touch_motion(
    uint32_t time_ms, int32_t id, wf::pointf_t at)
{
    auto seat = wf::get_core().get_current_seat();
    wlr_seat_touch_notify_motion(seat, time_ms, id, at.x, at.y);
}

void wf::wlr_surface_base_t::handle_touch_up(
    uint32_t time_ms, int32_t id, bool finger_lifted)
{
    auto seat = wf::get_core().get_current_seat();
    if (finger_lifted)
    {
        wlr_seat_touch_notify_up(seat, time_ms, id);
    }
}

wf::point_t wf::wlr_surface_base_t::get_offset()
{
    // A default offset, subsurfaces override this anyway
    return {0, 0};
}

wf::dimensions_t wf::wlr_surface_base_t::get_size() const
{
    if (!surface)
    {
        return {0, 0};
    }

    return {
        surface->current.width,
        surface->current.height,
    };
}

void wf::wlr_surface_base_t::schedule_redraw(const timespec& frame_end)
{
    if (surface)
    {
        wlr_surface_send_frame_done(surface, &frame_end);
    }
}

void wf::wlr_surface_base_t::set_visible_on_output(
    wf::output_t *output, bool is_visible)
{
    visibility[output] += (is_visible ? 1 : -1);

    if (!surface)
    {
        return;
    }

    if (is_visible && (visibility[output] == 1))
    {
        wlr_surface_send_enter(surface, output->handle);
    }

    if (!is_visible && (visibility[output] == 0))
    {
        wlr_surface_send_leave(surface, output->handle);
        // Do not leak memory for an output we are not on anymore
        visibility.erase(output);
    }
}

wf::region_t wf::wlr_surface_base_t::get_opaque_region()
{
    if (!is_mapped())
    {
        return {};
    }

    wf::region_t opaque{&surface->opaque_region};
    opaque.expand_edges(
        -wf::surface_interface_t::get_active_shrink_constraint());

    return opaque;
}

void wf::wlr_surface_base_t::simple_render(
    const wf::framebuffer_t& fb, wf::point_t pos, const wf::region_t& damage)
{
    if (!is_mapped())
    {
        return;
    }

    auto size = this->get_size();
    wf::geometry_t geometry = {pos.x, pos.y, size.width, size.height};
    wf::texture_t texture{surface};

    OpenGL::render_begin(fb);
    OpenGL::render_texture(texture, fb, geometry, glm::vec4(1.f),
        OpenGL::RENDER_FLAG_CACHED);
    for (const auto& rect : damage)
    {
        fb.logic_scissor(wlr_box_from_pixman_box(rect));
        OpenGL::draw_cached();
    }

    OpenGL::clear_cached();
    OpenGL::render_end();
}

bool wf::wlr_surface_base_t::is_mapped() const
{
    return mapped;
}

wlr_surface*wf::wlr_surface_base_t::get_wlr_surface()
{
    return surface;
}

wf::input_surface_t& wf::wlr_surface_base_t::input()
{
    return *this;
}

wf::output_surface_t& wf::wlr_surface_base_t::output()
{
    return *this;
}
