#include <string>
#include <wayfire/util/log.hpp>
#include <wayfire/debug.hpp>
#include "view/view-impl.hpp"
#include "wayfire/core.hpp"
#include "wayfire/seat.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/output.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/unstable/wlr-view-keyboard-interaction.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/view.hpp"
#include "xdg-shell.hpp"
#include "wayfire/output-layout.hpp"
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/unstable/wlr-surface-controller.hpp>
#include <wayfire/unstable/wlr-view-events.hpp>
#include <wayfire/nonstd/tracking-allocator.hpp>

#include "xdg-shell/xdg-toplevel-view.hpp"
#include <wayfire/view-helpers.hpp>

class wayfire_xdg_popup_node : public wf::scene::translation_node_t
{
  public:
    wayfire_xdg_popup_node(std::shared_ptr<wayfire_xdg_popup> popup) : id(popup->get_id())
    {
        this->_popup = popup;
        this->kb_interaction = std::make_unique<wf::wlr_view_keyboard_interaction_t>(popup, true);
    }

    std::string stringify() const override
    {
        return "xdg-popup view id=" + std::to_string(id) + " " + stringify_flags();
    }

    wf::keyboard_focus_node_t keyboard_refocus(wf::output_t *output) override
    {
        auto popup = _popup.lock();
        if (!popup)
        {
            return {};
        }

        if (!popup->is_mapped() || !popup->popup->seat || !popup->get_keyboard_focus_surface() ||
            (output != popup->get_output()))
        {
            return {};
        }

        return wf::keyboard_focus_node_t{
            .node = this,
            .importance = wf::focus_importance::REGULAR,
            .allow_focus_below = false,
        };
    }

    wf::keyboard_interaction_t& keyboard_interaction() override
    {
        return *kb_interaction;
    }

  private:
    uint64_t id = 0;
    std::weak_ptr<wayfire_xdg_popup> _popup;
    std::unique_ptr<wf::wlr_view_keyboard_interaction_t> kb_interaction;
};

bool wayfire_xdg_popup::should_close_on_focus_change(wf::keyboard_focus_changed_signal *ev)
{
    if (!is_mapped())
    {
        return false;
    }

    auto view = wf::node_to_view(ev->new_focus);
    const bool focus_client_changes = view && (view->get_client() != this->get_client());
    const bool has_grab = this->popup->seat != nullptr;

    if (has_grab)
    {
        return !view || focus_client_changes;
    } else
    {
        if ((ev->reason == wf::keyboard_focus_reason::UNKNOWN) ||
            (ev->reason == wf::keyboard_focus_reason::REFOCUS))
        {
            return false;
        }

        if ((ev->new_focus && !view) || focus_client_changes)
        {
            return true;
        }
    }

    return false;
}

wayfire_xdg_popup::wayfire_xdg_popup(wlr_xdg_popup *popup) : wf::view_interface_t()
{
    this->popup_parent = wf::wl_surface_to_wayfire_view(popup->parent->resource).get();
    this->popup = popup;
    this->role  = wf::VIEW_ROLE_UNMANAGED;

    if (!dynamic_cast<wayfire_xdg_popup*>(popup_parent.get()))
    {
        // 'toplevel' popups are responsible for closing their popup tree when the parent loses focus.
        // Note: we shouldn't close nested popups manually, since the parent popups will destroy them as well.
        this->on_keyboard_focus_changed = [=] (wf::keyboard_focus_changed_signal *ev)
        {
            if (should_close_on_focus_change(ev))
            {
                this->close();
            }
        };
    }

    on_surface_commit.set_callback([&] (void*) { commit(); });

    LOGI("New xdg popup");
    this->main_surface = std::make_shared<wf::scene::wlr_surface_node_t>(popup->base->surface, true);

    on_map.set_callback([&] (void*) { map(); });
    on_unmap.set_callback([&] (void*) { unmap(); });
    on_new_popup.set_callback([&] (void *data)
    {
        create_xdg_popup((wlr_xdg_popup*)data);
    });
    on_ping_timeout.set_callback([&] (void*)
    {
        wf::view_implementation::emit_ping_timeout_signal(self());
    });
    on_reposition.set_callback([&] (void*)
    {
        unconstrain();
    });

    on_map.connect(&popup->base->surface->events.map);
    on_unmap.connect(&popup->base->surface->events.unmap);
    on_new_popup.connect(&popup->base->events.new_popup);
    on_ping_timeout.connect(&popup->base->events.ping_timeout);
    on_reposition.connect(&popup->events.reposition);

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
}

wayfire_xdg_popup::~wayfire_xdg_popup() = default;

std::shared_ptr<wayfire_xdg_popup> wayfire_xdg_popup::create(wlr_xdg_popup *popup)
{
    auto self = wf::view_interface_t::create<wayfire_xdg_popup>(popup);

    self->surface_root_node = std::make_shared<wayfire_xdg_popup_node>(self);
    self->set_surface_root_node(self->surface_root_node);
    self->set_output(self->popup_parent->get_output());
    self->unconstrain();
    return self;
}

void wayfire_xdg_popup::map()
{
    LOGC(VIEWS, "Trying to map xdg-popup ", self());
    if (!get_output())
    {
        close();
        return;
    }

    update_position();

    wf::scene::layer parent_layer = wf::get_view_layer(popup_parent).value_or(wf::scene::layer::WORKSPACE);
    auto target_layer = wf::scene::layer::UNMANAGED;
    if ((int)parent_layer > (int)wf::scene::layer::WORKSPACE)
    {
        target_layer = parent_layer;
    }

    wf::scene::readd_front(get_output()->node_for_layer(target_layer), get_root_node());

    on_surface_commit.connect(&popup->base->surface->events.commit);
    priv->set_mapped_surface_contents(main_surface);
    priv->set_mapped(main_surface->get_surface());
    priv->set_enabled(true);
    update_size();

    damage();
    emit_view_map();
    wf::get_core().connect(&on_keyboard_focus_changed);

    if (popup->seat)
    {
        wf::get_core().seat->focus_view(self());
    }
}

void wayfire_xdg_popup::unmap()
{
    if (!is_mapped())
    {
        LOGC(VIEWS, "Denying unmap of unmapped xdg-popup ", self());
        return;
    }

    auto _self_ref = shared_from_this();
    LOGC(VIEWS, "Unmapping xdg-popup ", self());
    on_keyboard_focus_changed.disconnect();
    damage();
    emit_view_pre_unmap();

    priv->unset_mapped_surface_contents();
    priv->set_mapped(nullptr);
    on_surface_commit.disconnect();

    emit_view_unmap();
    priv->set_enabled(false);
}

void wayfire_xdg_popup::commit()
{
    update_size();
    update_position();
}

void wayfire_xdg_popup::update_position()
{
    if (!popup_parent->is_mapped() || !popup)
    {
        return;
    }

    // Offset relative to the parent surface
    wf::pointf_t local_offset = {
        popup->current.geometry.x * 1.0 - popup->base->current.geometry.x,
        popup->current.geometry.y * 1.0 - popup->base->current.geometry.y,
    };

    if (wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(popup->parent))
    {
        wlr_box box;
        wlr_xdg_surface_get_geometry(xdg_surface, &box);
        local_offset.x += box.x;
        local_offset.y += box.y;
    }

    wf::pointf_t popup_offset = wf::place_popup_at(popup->parent, popup->base->surface, local_offset);
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
    auto parent_offset = toplevel_parent->get_surface_root_node()->to_global({0, 0});
    box.x -= parent_offset.x;
    box.y -= parent_offset.y;
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
}

void wayfire_xdg_popup::destroy()
{
    on_map.disconnect();
    on_unmap.disconnect();
    on_new_popup.disconnect();
    on_ping_timeout.disconnect();
    on_reposition.disconnect();
    popup->base->data = nullptr;
    popup = nullptr;
}

void wayfire_xdg_popup::close()
{
    LOGC(VIEWS, "Closing xdg-popup ", self(), " ", is_mapped());
    if (is_mapped())
    {
        wlr_xdg_popup_destroy(popup);
    }
}

void wayfire_xdg_popup::ping()
{
    if (popup)
    {
        wlr_xdg_surface_ping(popup->base);
    }
}

void wayfire_xdg_popup::update_size()
{
    if (!is_mapped())
    {
        return;
    }

    wf::dimensions_t current_size{popup->base->surface->current.width, popup->base->surface->current.height};
    if (current_size == wf::dimensions(geometry))
    {
        return;
    }

    /* Damage current size */
    wf::scene::damage_node(get_root_node(), last_bounding_box);
    geometry.width  = current_size.width;
    geometry.height = current_size.height;

    /* Damage new size */
    last_bounding_box = get_bounding_box();
    wf::scene::damage_node(get_root_node(), last_bounding_box);
    wf::scene::update(this->get_surface_root_node(), wf::scene::update_flag::GEOMETRY);
}

bool wayfire_xdg_popup::is_mapped() const
{
    return priv->is_mapped;
}

void wayfire_xdg_popup::handle_app_id_changed(std::string new_app_id)
{
    this->app_id = new_app_id;
    wf::view_implementation::emit_app_id_changed_signal(self());
}

void wayfire_xdg_popup::handle_title_changed(std::string new_title)
{
    this->title = new_title;
    wf::view_implementation::emit_title_changed_signal(self());
}

std::string wayfire_xdg_popup::get_app_id()
{
    return this->app_id;
}

std::string wayfire_xdg_popup::get_title()
{
    return this->title;
}

void wayfire_xdg_popup::move(int x, int y)
{
    wf::scene::damage_node(get_root_node(), last_bounding_box);
    surface_root_node->set_offset({x, y});
    geometry.x = x;
    geometry.y = y;
    damage();
    last_bounding_box = get_bounding_box();
    wf::scene::update(this->get_surface_root_node(), wf::scene::update_flag::GEOMETRY);
}

wf::geometry_t wayfire_xdg_popup::get_geometry()
{
    return geometry;
}

wlr_surface*wayfire_xdg_popup::get_keyboard_focus_surface()
{
    return priv->wsurface;
}

/**
 * A class which manages the xdg_popup_view for the duration of the wlr_xdg_popup object lifetime.
 */
class xdg_popup_controller_t
{
    std::shared_ptr<wayfire_xdg_popup> view;
    wf::wl_listener_wrapper on_destroy;

  public:
    xdg_popup_controller_t(wlr_xdg_popup *popup)
    {
        on_destroy.set_callback([=] (auto)
        {
            view->destroy();
            delete this;
        });
        on_destroy.connect(&popup->events.destroy);
        view = wayfire_xdg_popup::create(popup);
    }

    ~xdg_popup_controller_t()
    {}
};

void create_xdg_popup(wlr_xdg_popup *popup)
{
    if (!wf::wl_surface_to_wayfire_view(popup->parent->resource))
    {
        LOGE("attempting to create a popup with unknown parent");
        return;
    }

    // Will be freed by the destroy handler
    new xdg_popup_controller_t{popup};
}

static wlr_xdg_shell *xdg_handle = nullptr;
static wf::wl_listener_wrapper on_xdg_created;

void wf::init_xdg_shell()
{
    xdg_handle = wlr_xdg_shell_create(wf::get_core().display, XDG_TOPLEVEL_STATE_SUSPENDED_SINCE_VERSION);
    if (xdg_handle)
    {
        on_xdg_created.set_callback([&] (void *data)
        {
            auto surf = static_cast<wlr_xdg_toplevel*>(data);
            wf::new_xdg_surface_signal new_xdg_surf;
            new_xdg_surf.surface = surf->base;
            wf::get_core().emit(
                &new_xdg_surf);
            if (new_xdg_surf.use_default_implementation && (surf->base->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL))
            {
                default_handle_new_xdg_toplevel(surf);
            }
        });
        on_xdg_created.connect(&xdg_handle->events.new_toplevel);
    }
}

void wf::fini_xdg_shell()
{
    on_xdg_created.disconnect();
}

bool wayfire_xdg_popup::is_focusable() const
{
    return false;
}
