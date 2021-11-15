#include <algorithm>
#include <map>
#include <wayfire/util/log.hpp>
#include "surface-impl.hpp"
#include "subsurface.hpp"
#include "wayfire/opengl.hpp"
#include "../core/core-impl.hpp"
#include "wayfire/output.hpp"
#include <wayfire/util/log.hpp>
#include "wayfire/render-manager.hpp"
#include "wayfire/signal-definitions.hpp"

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
    subsurface->set_output(get_output());
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

wf::surface_interface_t*wf::surface_interface_t::get_main_surface()
{
    if (priv->parent_surface)
    {
        return priv->parent_surface->get_main_surface();
    }

    return this;
}

std::vector<wf::surface_iterator_t> wf::surface_interface_t::enumerate_surfaces(
    wf::point_t surface_origin)
{
    std::vector<wf::surface_iterator_t> result;
    result.reserve(priv->last_cnt_surfaces);
    auto add_surfaces_recursive = [&] (surface_interface_t *child)
    {
        if (!child->is_mapped())
        {
            return;
        }

        auto child_surfaces = child->enumerate_surfaces(
            child->get_offset() + surface_origin);
        result.insert(result.end(),
            child_surfaces.begin(), child_surfaces.end());
    };

    for (auto& child : priv->surface_children_above)
    {
        add_surfaces_recursive(child.get());
    }

    if (is_mapped())
    {
        result.push_back({this, surface_origin});
    }

    for (auto& child : priv->surface_children_below)
    {
        add_surfaces_recursive(child.get());
    }

    priv->last_cnt_surfaces = result.size();
    return result;
}

wf::output_t*wf::surface_interface_t::get_output()
{
    return priv->output;
}

void wf::surface_interface_t::set_output(wf::output_t *output)
{
    priv->output = output;
    for (auto& c : priv->surface_children_above)
    {
        c->set_output(output);
    }

    for (auto& c : priv->surface_children_below)
    {
        c->set_output(output);
    }
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

/****************************
* surface_interface_t functions for surfaces which are
* backed by a wlr_surface
****************************/
void wf::surface_interface_t::send_frame_done(const timespec& time)
{
    if (priv->wsurface)
    {
        wlr_surface_send_frame_done(priv->wsurface, &time);
    }
}

bool wf::surface_interface_t::accepts_input(int32_t sx, int32_t sy)
{
    if (!priv->wsurface)
    {
        return false;
    }

    return wlr_surface_point_accepts_input(priv->wsurface, sx, sy);
}

wf::region_t wf::surface_interface_t::get_opaque_region(wf::point_t origin)
{
    if (!priv->wsurface)
    {
        return {};
    }

    wf::region_t opaque{&priv->wsurface->opaque_region};
    opaque += origin;
    opaque.expand_edges(-get_active_shrink_constraint());

    return opaque;
}

wl_client*wf::surface_interface_t::get_client()
{
    if (priv->wsurface)
    {
        return wl_resource_get_client(priv->wsurface->resource);
    }

    return nullptr;
}

wlr_surface*wf::surface_interface_t::get_wlr_surface()
{
    return priv->wsurface;
}

void wf::surface_interface_t::damage_surface_region(
    const wf::region_t& dmg)
{
    for (const auto& rect : dmg)
    {
        damage_surface_box(wlr_box_from_pixman_box(rect));
    }
}

void wf::surface_interface_t::damage_surface_box(const wlr_box& box)
{
    /* wlr_view_t overrides damage_surface_box and applies it to the output */
    if (priv->parent_surface && priv->parent_surface->is_mapped())
    {
        wlr_box parent_box = box;
        parent_box.x += get_offset().x;
        parent_box.y += get_offset().y;
        priv->parent_surface->damage_surface_box(parent_box);
    }
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

wf::wlr_surface_base_t::wlr_surface_base_t(surface_interface_t *self)
{
    _as_si = self;
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
        _as_si->add_subsurface(std::move(subsurface), false);
        if (sub->mapped)
        {
            ptr->map(sub->surface);
        }
    };

    on_new_subsurface.set_callback(handle_new_subsurface);
    on_commit.set_callback([&] (void*) { commit(); });
}

wf::wlr_surface_base_t::~wlr_surface_base_t()
{}



wf::point_t wf::wlr_surface_base_t::get_window_offset()
{
    return {0, 0};
}

bool wf::wlr_surface_base_t::_is_mapped() const
{
    return surface;
}

wf::dimensions_t wf::wlr_surface_base_t::_get_size() const
{
    if (!_is_mapped())
    {
        return {0, 0};
    }

    return {
        surface->current.width,
        surface->current.height,
    };
}

void wf::emit_map_state_change(wf::surface_interface_t *surface)
{
    std::string state =
        surface->is_mapped() ? "surface-mapped" : "surface-unmapped";

    surface_map_state_changed_signal data;
    data.surface = surface;
    wf::get_core().emit_signal(state, &data);
}

void wf::wlr_surface_base_t::map(wlr_surface *surface)
{
    assert(!this->surface && surface);
    this->surface = surface;

    _as_si->priv->wsurface = surface;

    /* force surface_send_enter(), and also check whether parent surface
     * output hasn't changed while we were unmapped */
    wf::output_t *output = _as_si->priv->parent_surface ?
        _as_si->priv->parent_surface->get_output() : _as_si->get_output();
    _as_si->set_output(output);

    on_new_subsurface.connect(&surface->events.new_subsurface);
    on_commit.connect(&surface->events.commit);

    surface->data = _as_si;

    /* Handle subsurfaces which were created before this surface was mapped */
    wlr_subsurface *sub;
    wl_list_for_each(sub, &surface->current.subsurfaces_below, current.link)
    handle_new_subsurface(sub);
    wl_list_for_each(sub, &surface->current.subsurfaces_above, current.link)
    handle_new_subsurface(sub);

    emit_map_state_change(_as_si);
}

void wf::wlr_surface_base_t::unmap()
{
    assert(this->surface);
    apply_surface_damage();
    _as_si->damage_surface_box({.x = 0, .y = 0,
        .width = _get_size().width, .height = _get_size().height});

    this->surface->data = NULL;
    this->surface = nullptr;
    this->_as_si->priv->wsurface = nullptr;
    emit_map_state_change(_as_si);

    on_new_subsurface.disconnect();
    on_destroy.disconnect();
    on_commit.disconnect();

    // Clear all subsurfaces we have.
    // This might remove subsurfaces that will be re-created again on map.
    this->_as_si->clear_subsurfaces();
}

wlr_buffer*wf::wlr_surface_base_t::get_buffer()
{
    if (surface && wlr_surface_has_buffer(surface))
    {
        return &surface->buffer->base;
    }

    return nullptr;
}

void wf::wlr_surface_base_t::apply_surface_damage()
{
    if (!_as_si->get_output() || !_is_mapped())
    {
        return;
    }

    wf::region_t dmg;
    wlr_surface_get_effective_damage(surface, dmg.to_pixman());

    if ((surface->current.scale != 1) ||
        (surface->current.scale != _as_si->get_output()->handle->scale))
    {
        dmg.expand_edges(1);
    }

    _as_si->damage_surface_region(dmg);
}

void wf::wlr_surface_base_t::commit()
{
    apply_surface_damage();
    if (_as_si->get_output())
    {
        /* we schedule redraw, because the surface might expect
         * a frame callback */
        _as_si->get_output()->render->schedule_redraw();
    }
}

void wf::wlr_surface_base_t::update_output(wf::output_t *old_output,
    wf::output_t *new_output)
{
    /* We should send send_leave only if the output is different from the last. */
    if (old_output && (old_output != new_output) && surface)
    {
        wlr_surface_send_leave(surface, old_output->handle);
    }

    if (new_output && surface)
    {
        wlr_surface_send_enter(surface, new_output->handle);
    }
}

void wf::wlr_surface_base_t::_simple_render(const wf::framebuffer_t& fb,
    int x, int y, const wf::region_t& damage)
{
    if (!get_buffer())
    {
        return;
    }

    auto size = this->_get_size();
    wf::geometry_t geometry = {x, y, size.width, size.height};
    wf::texture_t texture{surface};

    OpenGL::render_begin(fb);
    OpenGL::render_texture(texture, fb, geometry, glm::vec4(1.f),
        OpenGL::RENDER_FLAG_CACHED);
    // use GL_NEAREST for integer scale.
    // GL_NEAREST makes scaled text blocky instead of blurry, which looks better
    // but only for integer scale.
    if (fb.scale - floor(fb.scale) < 0.001)
    {
        GL_CALL(glTexParameteri(texture.target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    }

    for (const auto& rect : damage)
    {
        fb.logic_scissor(wlr_box_from_pixman_box(rect));
        OpenGL::draw_cached();
    }

    OpenGL::clear_cached();
    OpenGL::render_end();
}

wf::wlr_child_surface_base_t::wlr_child_surface_base_t(
    surface_interface_t *self) : wlr_surface_base_t(self)
{}

wf::wlr_child_surface_base_t::~wlr_child_surface_base_t()
{}
