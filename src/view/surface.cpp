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

wf::scene::node_ptr wf::surface_interface_t::get_content_node() const
{
    return priv->content_node;
}

wf::wlr_surface_controller_t::wlr_surface_controller_t(wlr_surface *surface,
    scene::floating_inner_ptr root_node)
{
    if (surface->data)
    {
        delete (wlr_surface_controller_t*)surface->data;
    }

    surface->data = this;
    this->surface = surface;
    this->root    = root_node;

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

    /* Handle subsurfaces which were created before the controller */
    wlr_subsurface *sub;
    wl_list_for_each(sub, &surface->current.subsurfaces_below, current.link)
    {
        on_new_subsurface.emit(sub);
    }

    wl_list_for_each(sub, &surface->current.subsurfaces_above, current.link)
    {
        on_new_subsurface.emit(sub);
    }
}

wf::wlr_surface_controller_t::~wlr_surface_controller_t()
{
    surface->data = nullptr;
}
