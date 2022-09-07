#include "subsurface.hpp"
#include "view/view-impl.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/debug.hpp>
#include <cassert>

wf::subsurface_implementation_t::subsurface_implementation_t(wlr_subsurface *_sub) :
    wlr_child_surface_base_t(this)
{
    this->sub = _sub;
    on_map.set_callback([&] (void*)
    {
        this->map(this->sub->surface);
    });
    on_unmap.set_callback([&] (void*) { this->unmap(); });
    on_destroy.set_callback([&] (void*)
    {
        on_map.disconnect();
        on_unmap.disconnect();
        on_destroy.disconnect();

        (void)this->priv->parent_surface->remove_subsurface(this);
    });

    on_map.connect(&sub->events.map);
    on_unmap.connect(&sub->events.unmap);
    on_destroy.connect(&sub->events.destroy);

    on_removed.set_callback([=] (auto data)
    {
        auto ev = static_cast<wf::subsurface_removed_signal*>(data);
        if ((ev->subsurface.get() == this) && this->is_mapped())
        {
            unmap();
        }
    });

    // At this point, priv->parent_surface is not set!
    auto parent = wf_surface_from_void(sub->parent->data);
    parent->connect_signal("subsurface-removed", &on_removed);
}

wf::point_t wf::subsurface_implementation_t::get_offset()
{
    if (!is_mapped())
    {
        return {0, 0};
    }

    return {
        sub->current.x,
        sub->current.y,
    };
}
