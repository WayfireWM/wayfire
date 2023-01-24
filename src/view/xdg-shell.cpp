#include <wayfire/util/log.hpp>
#include <wayfire/debug.hpp>
#include "view/view-impl.hpp"
#include "wayfire/core.hpp"
#include "surface-impl.hpp"
#include "wayfire/output.hpp"
#include "wayfire/decorator.hpp"
#include "wayfire/view.hpp"
#include "xdg-shell.hpp"
#include "wayfire/output-layout.hpp"
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

#include "xdg-shell/xdg-toplevel-view.hpp"

wayfire_xdg_popup::wayfire_xdg_popup(wlr_xdg_popup *popup) :
    wf::wlr_view_t()
{
    this->popup_parent = wf::wl_surface_to_wayfire_view(popup->parent->resource).get();
    this->popup = popup;
    this->role  = wf::VIEW_ROLE_UNMANAGED;
    this->priv->keyboard_focus_enabled = false;
    this->set_output(popup_parent->get_output());
}

void wayfire_xdg_popup::initialize()
{
    wf::wlr_view_t::initialize();
    LOGI("New xdg popup");
    on_map.set_callback([&] (void*) { map(this->popup->base->surface); });
    on_unmap.set_callback([&] (void*)
    {
        pending_close.disconnect();
        unmap();
    });
    on_destroy.set_callback([&] (void*) { destroy(); });
    on_new_popup.set_callback([&] (void *data)
    {
        create_xdg_popup((wlr_xdg_popup*)data);
    });
    on_ping_timeout.set_callback([&] (void*)
    {
        wf::emit_ping_timeout_signal(self());
    });

    on_map.connect(&popup->base->events.map);
    on_unmap.connect(&popup->base->events.unmap);
    on_destroy.connect(&popup->base->events.destroy);
    on_new_popup.connect(&popup->base->events.new_popup);
    on_ping_timeout.connect(&popup->base->events.ping_timeout);

    popup->base->data = this;
    parent_geometry_changed.set_callback([=] (auto)
    {
        this->update_position();
    });
    parent_app_id_changed.set_callback([=] (auto)
    {
        this->handle_app_id_changed(popup_parent->get_app_id());
    });
    parent_title_changed.set_callback([=] (auto)
    {
        this->handle_title_changed(popup_parent->get_title());
    });

    popup_parent->connect(&this->parent_geometry_changed);
    popup_parent->connect(&this->parent_app_id_changed);
    popup_parent->connect(&this->parent_title_changed);

    unconstrain();
}

void wayfire_xdg_popup::map(wlr_surface *surface)
{
    uint32_t parent_layer =
        get_output()->workspace->get_view_layer(popup_parent->self());

    wf::layer_t target_layer = wf::LAYER_UNMANAGED;
    if (parent_layer > wf::LAYER_WORKSPACE)
    {
        target_layer = (wf::layer_t)parent_layer;
    }

    get_output()->workspace->add_view(self(), target_layer);

    wlr_view_t::map(surface);
    update_position();
}

void wayfire_xdg_popup::commit()
{
    wlr_view_t::commit();
    update_position();
}

void wayfire_xdg_popup::update_position()
{
    if (!popup_parent->is_mapped() || !is_mapped())
    {
        return;
    }

    wf::pointf_t popup_offset = {1.0 * popup->current.geometry.x, 1.0 * popup->current.geometry.y};
    if (wlr_surface_is_xdg_surface(popup->parent))
    {
        wlr_box box;
        wlr_xdg_surface_get_geometry(wlr_xdg_surface_from_wlr_surface(popup->parent), &box);
        popup_offset.x += box.x;
        popup_offset.y += box.y;
    }

    auto parent_geometry = popup_parent->get_output_geometry();
    popup_offset.x += parent_geometry.x - popup->base->current.geometry.x;
    popup_offset.y += parent_geometry.y - popup->base->current.geometry.y;

    // Apply transformers to the popup position
    auto node = popup_parent->get_surface_root_node()->parent();
    while (node != popup_parent->get_transformed_node().get())
    {
        popup_offset = node->to_global(popup_offset);
        node = node->parent();
    }

    this->move(popup_offset.x, popup_offset.y);
}

void wayfire_xdg_popup::unconstrain()
{
    wf::view_interface_t *toplevel_parent = this;
    while (true)
    {
        auto as_popup = dynamic_cast<wayfire_xdg_popup*>(toplevel_parent);
        if (as_popup)
        {
            toplevel_parent = as_popup->popup_parent.get();
        } else
        {
            break;
        }
    }

    if (!get_output() || !toplevel_parent)
    {
        return;
    }

    auto box = get_output()->get_relative_geometry();
    auto wm  = toplevel_parent->get_output_geometry();
    box.x -= wm.x;
    box.y -= wm.y;

    wlr_xdg_popup_unconstrain_from_box(popup, &box);
}

void wayfire_xdg_popup::destroy()
{
    on_map.disconnect();
    on_unmap.disconnect();
    on_destroy.disconnect();
    on_new_popup.disconnect();
    on_ping_timeout.disconnect();

    wlr_view_t::destroy();
}

void wayfire_xdg_popup::close()
{
    pending_close.run_once([=] ()
    {
        if (is_mapped())
        {
            wlr_xdg_popup_destroy(popup);
        }
    });
}

void wayfire_xdg_popup::ping()
{
    if (popup)
    {
        wlr_xdg_surface_ping(popup->base);
    }
}

void create_xdg_popup(wlr_xdg_popup *popup)
{
    if (!wf::wl_surface_to_wayfire_view(popup->parent->resource))
    {
        LOGE("attempting to create a popup with unknown parent");
        return;
    }

    wf::get_core().add_view(std::make_unique<wayfire_xdg_popup>(popup));
}

static wlr_xdg_shell *xdg_handle = nullptr;

void wf::init_xdg_shell()
{
    static wf::wl_listener_wrapper on_xdg_created;
    xdg_handle = wlr_xdg_shell_create(wf::get_core().display, 2);

    if (xdg_handle)
    {
        on_xdg_created.set_callback([&] (void *data)
        {
            auto surf = static_cast<wlr_xdg_surface*>(data);
            if (surf->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
            {
                wf::get_core().add_view(std::make_unique<wf::xdg_toplevel_view_t>(surf->toplevel));
            }
        });
        on_xdg_created.connect(&xdg_handle->events.new_surface);
    }
}
