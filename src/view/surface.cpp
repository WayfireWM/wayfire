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

wf::wlr_surface_controller_t::wlr_surface_controller_t(wlr_surface *surface,
    scene::floating_inner_ptr root_node)
{
    try_free_controller(surface);

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
        create_controller(sub->surface, sub_controller->get_subsurface_root());
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

void wf::wlr_surface_controller_t::create_controller(
    wlr_surface *surface, scene::floating_inner_ptr root_node)
{
    new wlr_surface_controller_t(surface, root_node);
}

void wf::wlr_surface_controller_t::try_free_controller(wlr_surface *surface)
{
    if (surface->data)
    {
        delete (wlr_surface_controller_t*)surface->data;
    }
}
