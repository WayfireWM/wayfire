#include <wayfire/util/log.hpp>
#include <wayfire/util/log.hpp>
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/unstable/wlr-surface-controller.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"
#include "wayfire/unstable/wlr-subsurface-controller.hpp"
#include <wayfire/scene-operations.hpp>

static void update_subsurface_position(wlr_surface *surface, int, int, void*)
{
    if (wlr_subsurface *sub = wlr_subsurface_try_from_wlr_surface(surface))
    {
        if (sub->data)
        {
            auto sub_root = ((wf::wlr_subsurface_controller_t*)sub->data)->get_subsurface_root();
            sub_root->update_offset();
        }
    }
}

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
        wlr_subsurface *s;
        wl_list_for_each(s, &surface->current.subsurfaces_below, current.link)
        {
            if (sub == s)
            {
                wf::scene::add_back(this->root, sub_controller->get_subsurface_root());
                return;
            }
        }
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

    on_commit.set_callback([=] (void*)
    {
        if (!wlr_subsurface_try_from_wlr_surface(surface))
        {
            wlr_surface_for_each_surface(surface, update_subsurface_position, nullptr);
        }

        update_subsurface_order_and_position();
    });

    on_commit.connect(&surface->events.commit);
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

void wf::wlr_surface_controller_t::update_subsurface_order_and_position()
{
    auto old_bbox = root->get_bounding_box();

    // Calculate whether we need to reorder the surfaces.
    auto all_subsurfaces = this->root->get_children();

    // Go until we find the main node, it should be a wlr_surface node.
    auto it = std::find_if(all_subsurfaces.begin(), all_subsurfaces.end(),
        [=] (const scene::node_ptr& node)
    {
        if (auto wlr_node = dynamic_cast<scene::wlr_surface_node_t*>(node.get()))
        {
            return wlr_node->get_surface() == surface;
        }

        return false;
    });

    if (it == all_subsurfaces.end())
    {
        // Maybe unmapped? Can't do anything at the moment anyway.
        return;
    }

    // Now, put all subsurfaces above in the right order, then main view, then subsurfaces below.
    // For nodes that we have not copied, we put them at the start/end depending on their original
    // position.
    std::vector<scene::node_ptr> new_subsurface_order;

    // Update subsurface order
    bool subsurface_repositioned = false;

    wlr_subsurface *sub;
    wl_list_for_each_reverse(sub, &surface->current.subsurfaces_above, current.link)
    {
        auto sub_root = ((wf::wlr_subsurface_controller_t*)sub->data)->get_subsurface_root();
        new_subsurface_order.push_back(sub_root);
        subsurface_repositioned |= sub_root->update_offset(false);
    }
    new_subsurface_order.push_back(*it);
    wl_list_for_each_reverse(sub, &surface->current.subsurfaces_below, current.link)
    {
        auto sub_root = ((wf::wlr_subsurface_controller_t*)sub->data)->get_subsurface_root();
        new_subsurface_order.push_back(sub_root);
        subsurface_repositioned |= sub_root->update_offset(false);
    }

    // Place compositor subsurfaces correctly: either on top or below.
    for (auto iter = all_subsurfaces.begin(); iter != all_subsurfaces.end(); ++iter)
    {
        if (!dynamic_cast<scene::wlr_surface_node_t*>(iter->get()) &&
            !dynamic_cast<wlr_subsurface_root_node_t*>(iter->get()))
        {
            if (iter < it)
            {
                new_subsurface_order.insert(new_subsurface_order.begin(), *iter);
            } else
            {
                new_subsurface_order.push_back(*iter);
            }
        }
    }

    const bool order_changed = new_subsurface_order != all_subsurfaces;
    if (order_changed || subsurface_repositioned)
    {
        wf::scene::damage_node(root, old_bbox);
        if (order_changed)
        {
            root->set_children_list(new_subsurface_order);
            wf::scene::update(root, wf::scene::update_flag::CHILDREN_LIST);
        }

        wf::scene::damage_node(root, root->get_bounding_box());
    }
}
