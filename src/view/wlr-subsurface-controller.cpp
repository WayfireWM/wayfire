#include "wayfire/geometry.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/unstable/wlr-subsurface-controller.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"
#include <memory>
#include <wayfire/debug.hpp>

wf::wlr_subsurface_controller_t::wlr_subsurface_controller_t(wlr_subsurface *sub)
{
    if (sub->data)
    {
        delete (wlr_subsurface_controller_t*)sub->data;
    }

    this->sub = sub;
    sub->data = this;

    auto surface_node = std::make_shared<scene::wlr_surface_node_t>(sub->surface, true);
    if (!sub->surface->mapped)
    {
        surface_node->set_enabled(false);
    }

    this->subsurface_root_node = std::make_shared<wlr_subsurface_root_node_t>(sub);
    subsurface_root_node->set_children_list({surface_node});

    on_map.set_callback([=] (void*)
    {
        wf::scene::set_node_enabled(surface_node, true);
    });
    on_unmap.set_callback([=] (void*)
    {
        wf::scene::set_node_enabled(surface_node, false);
    });

    on_destroy.set_callback([=] (void*)
    {
        wf::scene::remove_child(subsurface_root_node);
        sub->data = NULL;
        delete this;
    });

    on_map.connect(&sub->surface->events.map);
    on_unmap.connect(&sub->surface->events.unmap);
    on_destroy.connect(&sub->events.destroy);
}

std::shared_ptr<wf::wlr_subsurface_root_node_t> wf::wlr_subsurface_controller_t::get_subsurface_root()
{
    return this->subsurface_root_node;
}

wf::wlr_subsurface_root_node_t::wlr_subsurface_root_node_t(wlr_subsurface *subsurface)
{
    this->subsurface = subsurface;
    this->on_subsurface_commit.set_callback([=] (void*)
    {
        update_offset();
    });

    this->on_subsurface_destroy.set_callback([=] (void*)
    {
        this->subsurface = NULL;
        on_subsurface_destroy.disconnect();
        on_subsurface_commit.disconnect();
    });

    on_subsurface_destroy.connect(&subsurface->events.destroy);
    on_subsurface_commit.connect(&subsurface->surface->events.commit);
    // Set initial offset but don't damage yet
    this->offset = {subsurface->current.x, subsurface->current.y};
}

std::string wf::wlr_subsurface_root_node_t::stringify() const
{
    return "subsurface root node";
}

bool wf::wlr_subsurface_root_node_t::update_offset(bool apply_damage)
{
    wf::point_t offset = {subsurface->current.x, subsurface->current.y};
    const bool changed = offset != get_offset();

    if (changed && apply_damage)
    {
        scene::damage_node(this, get_bounding_box());
        set_offset(offset);
        scene::damage_node(this, get_bounding_box());
    } else if (changed)
    {
        set_offset(offset);
    } else if (apply_damage)
    {
        scene::damage_node(this, get_bounding_box());
    }

    return changed;
}
