#include <algorithm>
#include <map>
#include <wayfire/util/log.hpp>
#include "surface-impl.hpp"
#include "subsurface.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "../core/core-impl.hpp"
#include "wayfire/output.hpp"
#include <wayfire/util/log.hpp>
#include "wayfire/render-manager.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"

#include <wayfire/scene-operations.hpp>

#include "surface-root-node.cpp"
#include "surface-node.cpp"

/****************************
* surface_interface_t functions
****************************/
wf::surface_interface_t::surface_interface_t()
{
    this->priv = std::make_unique<impl>();
    this->priv->parent_surface = nullptr;

    this->priv->content_node = std::make_shared<wf::scene::surface_node_t>(this);
    this->priv->root_node    = std::make_shared<wf::scene::surface_root_node_t>(
        this);
}

void wf::surface_interface_t::add_subsurface(
    std::unique_ptr<surface_interface_t> subsurface, bool is_below_parent)
{
    subsurface->priv->parent_surface = this;
    if (is_below_parent)
    {
        wf::scene::add_back(priv->root_node, subsurface->priv->root_node);
    } else
    {
        wf::scene::add_front(priv->root_node, subsurface->priv->root_node);
    }

    auto& container = is_below_parent ?
        priv->surface_children_below : priv->surface_children_above;
    container.insert(container.begin(), std::move(subsurface));
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

    wf::scene::remove_child(subsurface->priv->root_node);
    if (auto surf = remove_from(priv->surface_children_above))
    {
        return surf;
    }

    return remove_from(priv->surface_children_below);
}

wf::surface_interface_t::~surface_interface_t()
{}

/****************************
* surface_interface_t functions for surfaces which are
* backed by a wlr_surface
****************************/
bool wf::surface_interface_t::accepts_input(int32_t sx, int32_t sy)
{
    if (!priv->wsurface || !is_mapped())
    {
        return false;
    }

    return wlr_surface_point_accepts_input(priv->wsurface, sx, sy);
}

wf::region_t wf::surface_interface_t::get_opaque_region(wf::point_t origin)
{
    if (!priv->wsurface || is_mapped())
    {
        return {};
    }

    wf::region_t opaque{&priv->wsurface->opaque_region};
    opaque += origin;

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

void wf::surface_interface_t::clear_subsurfaces()
{
    const auto& finish_subsurfaces = [&] (auto& container)
    {
        this->priv->root_node->set_children_list({this->get_content_node()});
        scene::update(priv->root_node, scene::update_flag::CHILDREN_LIST);
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

        // Will be freed when the wlr_subsurface is destroyed
        auto controller = new wlr_subsurface_controller_t(sub);
        wf::scene::add_front(_as_si->priv->root_node, controller->get_subsurface_root());
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
    wf::scene::update(surface->get_content_node(),
        wf::scene::update_flag::INPUT_STATE);
}

void wf::wlr_surface_base_t::map(wlr_surface *surface)
{
    assert(!this->surface && surface);
    this->surface = surface;

    _as_si->priv->wsurface = surface;

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
    wf::scene::damage_node(_as_si->get_content_node(), wf::construct_box({0, 0}, _get_size()));

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

void wf::wlr_surface_base_t::commit()
{
    wf::region_t dmg;
    wlr_surface_get_effective_damage(surface, dmg.to_pixman());
    wf::scene::damage_node(_as_si->get_content_node(), dmg);
}

void wf::wlr_surface_base_t::_simple_render(const wf::render_target_t& fb,
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

wf::scene::node_ptr wf::surface_interface_t::get_content_node() const
{
    return priv->content_node;
}

wf::wlr_surface_controller_t::wlr_surface_controller_t(wlr_surface *surface,
    scene::floating_inner_ptr root_node)
{
    this->root = root_node;

    on_destroy.set_callback([=] (void*)
    {
        delete this;
    });

    on_new_subsurface.set_callback([=] (void *data)
    {
        auto sub = static_cast<wlr_subsurface*>(data);
        // Allocate memory, it will be auto-freed when the wlr objects are destroyed
        auto sub_controller = new wlr_subsurface_controller_t(sub);
        new wlr_surface_controller_t(sub->surface, sub_controller->get_subsurface_root());
        wf::scene::add_front(this->root, sub_controller->get_subsurface_root());
    });

    on_destroy.connect(&surface->events.destroy);
    on_new_subsurface.connect(&surface->events.new_subsurface);
}
