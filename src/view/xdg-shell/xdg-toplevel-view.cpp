#include "xdg-toplevel-view.hpp"
#include <wayfire/scene-operations.hpp>
#include "view/toplevel-node.hpp"
#include "wayfire/core.hpp"
#include <wayfire/txn/transaction.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include "../view-impl.hpp"
#include "../xdg-shell.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/seat.hpp"
#include "wayfire/util.hpp"
#include "wayfire/view.hpp"
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-set.hpp>
#include <wlr/util/edges.h>
#include <wayfire/window-manager.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/unstable/wlr-view-events.hpp>
#include "../core/seat/seat-impl.hpp"


// --------------------------------------- xdg-toplevel-base impl --------------------------------------------
wf::xdg_toplevel_view_base_t::xdg_toplevel_view_base_t(wlr_xdg_toplevel *toplevel, bool autocommit)
{
    this->xdg_toplevel = toplevel;
    LOGI("new xdg_shell_stable surface: ", xdg_toplevel->title, " app-id: ", xdg_toplevel->app_id);

    this->main_surface = std::make_shared<scene::wlr_surface_node_t>(toplevel->base->surface, autocommit);

    on_destroy.set_callback([&] (void*) { destroy(); });
    on_new_popup.set_callback([&] (void *data)
    {
        create_xdg_popup((decltype(xdg_toplevel->base->popup))data);
    });

    on_set_title.set_callback([&] (void*)
    {
        handle_title_changed(nonull(xdg_toplevel->title));
    });
    on_set_app_id.set_callback([&] (void*)
    {
        handle_app_id_changed(nonull(xdg_toplevel->app_id));
    });
    on_ping_timeout.set_callback([&] (void*)
    {
        wf::view_implementation::emit_ping_timeout_signal(self());
    });

    on_destroy.connect(&xdg_toplevel->events.destroy);
    on_new_popup.connect(&xdg_toplevel->base->events.new_popup);
    on_ping_timeout.connect(&xdg_toplevel->base->events.ping_timeout);
    on_set_title.connect(&xdg_toplevel->events.set_title);
    on_set_app_id.connect(&xdg_toplevel->events.set_app_id);

    xdg_toplevel->base->data = dynamic_cast<view_interface_t*>(this);
}

void wf::xdg_toplevel_view_base_t::map()
{
    LOGC(VIEWS, "Do map ", self());
    priv->set_mapped(main_surface->get_surface());
    priv->set_mapped_surface_contents(main_surface);
    priv->set_enabled(true);
    damage();
    emit_view_map();
}

void wf::xdg_toplevel_view_base_t::unmap()
{
    LOGC(VIEWS, "Do unmap ", self());
    damage();
    priv->unset_mapped_surface_contents();
    priv->set_mapped(nullptr);

    emit_view_unmap();
    priv->set_enabled(false);
    wf::scene::update(get_surface_root_node(), wf::scene::update_flag::INPUT_STATE);
}

wf::xdg_toplevel_view_base_t::~xdg_toplevel_view_base_t()
{
    if (xdg_toplevel && (xdg_toplevel->base->data == dynamic_cast<view_interface_t*>(this)))
    {
        xdg_toplevel->base->data = nullptr;
    }
}

void wf::xdg_toplevel_view_base_t::destroy()
{
    on_destroy.disconnect();
    on_new_popup.disconnect();
    on_set_title.disconnect();
    on_set_app_id.disconnect();
    on_ping_timeout.disconnect();

    priv->wsurface = nullptr;
    xdg_toplevel   = nullptr;
}

void wf::xdg_toplevel_view_base_t::handle_title_changed(std::string new_title)
{
    this->title = new_title;
    wf::view_implementation::emit_title_changed_signal(self());
}

void wf::xdg_toplevel_view_base_t::handle_app_id_changed(std::string new_app_id)
{
    this->app_id = new_app_id;
    wf::view_implementation::emit_app_id_changed_signal(self());
}

void wf::xdg_toplevel_view_base_t::close()
{
    if (xdg_toplevel)
    {
        wlr_xdg_toplevel_send_close(xdg_toplevel);
        view_interface_t::close();
    }
}

void wf::xdg_toplevel_view_base_t::ping()
{
    if (xdg_toplevel)
    {
        wlr_xdg_surface_ping(xdg_toplevel->base);
    }
}

wlr_surface*wf::xdg_toplevel_view_base_t::get_keyboard_focus_surface()
{
    if (xdg_toplevel && is_mapped())
    {
        return xdg_toplevel->base->surface;
    }

    return nullptr;
}

bool wf::xdg_toplevel_view_base_t::is_focusable() const
{
    return true;
}

std::string wf::xdg_toplevel_view_base_t::get_app_id()
{
    return app_id;
}

std::string wf::xdg_toplevel_view_base_t::get_title()
{
    return title;
}

bool wf::xdg_toplevel_view_base_t::is_mapped() const
{
    return priv->is_mapped;
}

// ------------------------------------------ xdg-toplevel impl ----------------------------------------------
/**
 * When we get a request for setting CSD, the view might not have been
 * created. So, we store all requests in core, and the views pick this
 * information when they are created
 */
std::unordered_map<wlr_surface*, uint32_t> uses_csd;

wf::xdg_toplevel_view_t::xdg_toplevel_view_t(wlr_xdg_toplevel *tlvl) : xdg_toplevel_view_base_t(tlvl, false)
{
    this->wtoplevel = std::make_shared<xdg_toplevel_t>(tlvl, this->main_surface);
    this->wtoplevel->connect(&this->on_toplevel_applied);
    this->priv->toplevel = this->wtoplevel;

    this->on_toplevel_applied = [&] (xdg_toplevel_applied_state_signal *ev)
    {
        this->handle_toplevel_state_changed(ev->old_state);
    };

    on_show_window_menu.set_callback([&] (void *data)
    {
        wlr_xdg_toplevel_show_window_menu_event *event =
            (wlr_xdg_toplevel_show_window_menu_event*)data;
        auto view   = self();
        auto output = view->get_output();
        if (!output)
        {
            return;
        }

        wf::view_show_window_menu_signal d;
        d.view = view;
        d.relative_position.x = event->x;
        d.relative_position.y = event->y;
        output->emit(&d);
        wf::get_core().emit(&d);
    });
    on_set_parent.set_callback([&] (void*)
    {
        auto parent =
            xdg_toplevel->parent ? (wf::view_interface_t*)(xdg_toplevel->parent->base->data) : nullptr;
        set_toplevel_parent(toplevel_cast(parent));
    });

    on_request_move.set_callback([&] (void *data)
    {
        auto ev = static_cast<wlr_xdg_toplevel_move_event*>(data);
        if (ev->serial == wf::get_core().seat->priv->last_press_release_serial)
        {
            wf::get_core().default_wm->move_request({this});
            return;
        }
    });
    on_request_resize.set_callback([&] (auto data)
    {
        auto ev = static_cast<wlr_xdg_toplevel_resize_event*>(data);
        if (ev->serial == wf::get_core().seat->priv->last_press_release_serial)
        {
            wf::get_core().default_wm->resize_request({this}, ev->edges);
        }
    });
    on_request_minimize.set_callback([&] (void*)
    {
        wf::get_core().default_wm->minimize_request({this}, true);
    });
    on_request_maximize.set_callback([&] (void *data)
    {
        wf::get_core().default_wm->tile_request({this},
            xdg_toplevel->requested.maximized ? wf::TILED_EDGES_ALL : 0);
    });
    on_request_fullscreen.set_callback([&] (void *data)
    {
        wlr_xdg_toplevel_requested *req = &xdg_toplevel->requested;
        auto wo = wf::get_core().output_layout->find_output(req->fullscreen_output);
        wf::get_core().default_wm->fullscreen_request({this}, wo, req->fullscreen);
    });

    on_set_parent.connect(&xdg_toplevel->events.set_parent);
    on_request_move.connect(&xdg_toplevel->events.request_move);
    on_request_resize.connect(&xdg_toplevel->events.request_resize);
    on_request_maximize.connect(&xdg_toplevel->events.request_maximize);
    on_request_minimize.connect(&xdg_toplevel->events.request_minimize);
    on_show_window_menu.connect(&xdg_toplevel->events.request_show_window_menu);
    on_request_fullscreen.connect(&xdg_toplevel->events.request_fullscreen);

    if (xdg_toplevel && uses_csd.count(xdg_toplevel->base->surface))
    {
        this->has_client_decoration = uses_csd[xdg_toplevel->base->surface];
    }
}

std::shared_ptr<wf::xdg_toplevel_view_t> wf::xdg_toplevel_view_t::create(wlr_xdg_toplevel *toplevel)
{
    auto self = view_interface_t::create<xdg_toplevel_view_t>(toplevel);

    self->surface_root_node = std::make_shared<toplevel_view_node_t>(self);
    self->set_surface_root_node(self->surface_root_node);

    // Set the output early, so that we can emit the signals on the output
    self->set_output(wf::get_core().seat->get_active_output());

    self->handle_title_changed(nonull(toplevel->title));
    self->handle_app_id_changed(nonull(toplevel->app_id));
    // set initial parent
    self->on_set_parent.emit(nullptr);

    if (toplevel->requested.fullscreen)
    {
        wf::get_core().default_wm->fullscreen_request(self, self->get_output(), true);
    }

    if (toplevel->requested.maximized)
    {
        wf::get_core().default_wm->tile_request(self, TILED_EDGES_ALL);
    }

    return self;
}

void wf::xdg_toplevel_view_t::request_native_size()
{
    this->wtoplevel->request_native_size();
}

void wf::xdg_toplevel_view_t::set_activated(bool active)
{
    toplevel_view_interface_t::set_activated(active);
    if (xdg_toplevel && xdg_toplevel->base->surface->mapped)
    {
        wlr_xdg_toplevel_set_activated(xdg_toplevel, active);
    } else if (xdg_toplevel)
    {
        xdg_toplevel->pending.activated = active;
    }
}

bool wf::xdg_toplevel_view_t::should_be_decorated()
{
    return !has_client_decoration;
}

bool wf::xdg_toplevel_view_t::is_mapped() const
{
    return wtoplevel->current().mapped && priv->is_mapped;
}

void wf::xdg_toplevel_view_t::map()
{
    adjust_view_output_on_map(this);

    xdg_toplevel_view_base_t::map();
    wf::get_core().default_wm->focus_request(self());

    /* Might trigger repositioning */
    set_toplevel_parent(this->parent);
}

void wf::xdg_toplevel_view_t::handle_toplevel_state_changed(wf::toplevel_state_t old_state)
{
    surface_root_node->set_offset(wf::origin(wtoplevel->calculate_base_geometry()));
    if (!old_state.mapped && wtoplevel->current().mapped)
    {
        map();
    }

    if (old_state.mapped && !wtoplevel->current().mapped)
    {
        unmap();
    }

    wf::view_implementation::emit_toplevel_state_change_signals({this}, old_state);
    scene::update(this->get_surface_root_node(), scene::update_flag::GEOMETRY);

    if (!wf::get_core().tx_manager->is_object_pending(wtoplevel))
    {
        // Drop self-ref => `this` might get deleted
        _self_ref.reset();
    }
}

void wf::xdg_toplevel_view_t::destroy()
{
    wf::xdg_toplevel_view_base_t::destroy();
    on_set_parent.disconnect();
    on_request_move.disconnect();
    on_request_resize.disconnect();
    on_request_maximize.disconnect();
    on_request_minimize.disconnect();
    on_show_window_menu.disconnect();
    on_request_fullscreen.disconnect();
}

void wf::xdg_toplevel_view_t::set_decoration_mode(bool use_csd)
{
    bool was_decorated = should_be_decorated();
    this->has_client_decoration = use_csd;
    if (was_decorated != should_be_decorated())
    {
        wf::view_decoration_state_updated_signal data;
        data.view = {this};

        this->emit(&data);
        wf::get_core().emit(&data);
    }
}

/* decorations impl */
struct wf_server_decoration_t
{
    wlr_server_decoration *decor;
    wf::wl_listener_wrapper on_mode_set, on_destroy;

    std::function<void(void*)> mode_set = [&] (void*)
    {
        bool use_csd = decor->mode == WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
        uses_csd[decor->surface] = use_csd;

        auto view = dynamic_cast<wf::xdg_toplevel_view_t*>(
            wf::wl_surface_to_wayfire_view(decor->surface->resource).get());
        if (view)
        {
            view->set_decoration_mode(use_csd);
        }
    };

    wf_server_decoration_t(wlr_server_decoration *_decor) :
        decor(_decor)
    {
        on_mode_set.set_callback(mode_set);
        on_destroy.set_callback([&] (void*)
        {
            uses_csd.erase(decor->surface);
            delete this;
        });

        on_mode_set.connect(&decor->events.mode);
        on_destroy.connect(&decor->events.destroy);
        /* Read initial decoration settings */
        mode_set(NULL);
    }
};

struct wf_xdg_decoration_t
{
    wlr_xdg_toplevel_decoration_v1 *decor;
    wf::wl_listener_wrapper on_mode_request, on_commit, on_destroy;

    wf::option_wrapper_t<std::string> deco_mode{"core/preferred_decoration_mode"};
    wf::option_wrapper_t<bool> force_preferred{"workarounds/force_preferred_decoration_mode"};

    std::function<void(void*)> mode_request = [&] (void*)
    {
        wlr_xdg_toplevel_decoration_v1_mode default_mode =
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        if ((std::string)deco_mode == "server")
        {
            default_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        }

        auto mode = decor->requested_mode;
        if ((mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE) || force_preferred)
        {
            mode = default_mode;
        }

        wlr_xdg_toplevel_decoration_v1_set_mode(decor, mode);
    };

    std::function<void(void*)> commit = [&] (void*)
    {
        bool use_csd = (decor->current.mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
        uses_csd[decor->toplevel->base->surface] = use_csd;

        auto wf_surface = dynamic_cast<wf::xdg_toplevel_view_t*>(
            wf::wl_surface_to_wayfire_view(decor->toplevel->base->surface->resource).get());
        if (wf_surface)
        {
            wf_surface->set_decoration_mode(use_csd);
        }
    };

    wf_xdg_decoration_t(wlr_xdg_toplevel_decoration_v1 *_decor) :
        decor(_decor)
    {
        on_mode_request.set_callback(mode_request);
        on_commit.set_callback(commit);
        on_destroy.set_callback([&] (void*)
        {
            uses_csd.erase(decor->toplevel->base->surface);
            delete this;
        });

        on_mode_request.connect(&decor->events.request_mode);
        on_commit.connect(&decor->toplevel->base->surface->events.commit);
        on_destroy.connect(&decor->events.destroy);
        /* Read initial decoration settings */
        mode_request(NULL);
    }
};

static wf::wl_listener_wrapper on_org_kde_decoration_created;
static void init_legacy_decoration()
{
    static wf::option_wrapper_t<std::string> deco_mode{"core/preferred_decoration_mode"};
    uint32_t default_mode = WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
    if ((std::string)deco_mode == "server")
    {
        default_mode = WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
    }

    wlr_server_decoration_manager_set_default_mode(wf::get_core().protocols.decorator_manager, default_mode);

    on_org_kde_decoration_created.set_callback([&] (void *data)
    {
        /* will be freed by the destroy request */
        new wf_server_decoration_t((wlr_server_decoration*)(data));
    });
    on_org_kde_decoration_created.connect(&wf::get_core().protocols.decorator_manager->events.new_decoration);
    deco_mode.set_callback([&] ()
    {
        uint32_t default_mode = WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
        if ((std::string)deco_mode == "server")
        {
            default_mode = WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
        }

        wlr_server_decoration_manager_set_default_mode(wf::get_core().protocols.decorator_manager,
            default_mode);
    });
}

static wf::wl_listener_wrapper on_xdg_decoration_created;
static void init_xdg_decoration()
{
    on_xdg_decoration_created.set_callback([&] (void *data)
    {
        /* will be freed by the destroy request */
        new wf_xdg_decoration_t((wlr_xdg_toplevel_decoration_v1*)(data));
    });

    on_xdg_decoration_created.connect(&wf::get_core().protocols.xdg_decorator->events.new_toplevel_decoration);
}

void wf::init_xdg_decoration_handlers()
{
    init_legacy_decoration();
    init_xdg_decoration();
}

void wf::fini_xdg_decoration_handlers()
{
    on_org_kde_decoration_created.disconnect();
    on_xdg_decoration_created.disconnect();
}

void wf::xdg_toplevel_view_t::start_map_tx()
{
    LOGC(VIEWS, "Start mapping ", self());
    wlr_box box;
    wlr_xdg_surface_get_geometry(xdg_toplevel->base, &box);

    auto margins = wtoplevel->pending().margins;
    box.x = wtoplevel->pending().geometry.x + margins.left;
    box.y = wtoplevel->pending().geometry.y + margins.top;

    wtoplevel->pending().mapped = true;
    priv->set_mapped_surface_contents(main_surface);
    adjust_view_pending_geometry_on_start_map(this, box, pending_fullscreen(), pending_tiled_edges());
    wf::get_core().tx_manager->schedule_object(wtoplevel);
}

void wf::xdg_toplevel_view_t::start_unmap_tx()
{
    LOGC(VIEWS, "Start unmapping ", self());
    emit_view_pre_unmap();

    // Take reference until the view has been unmapped
    _self_ref = shared_from_this();
    wtoplevel->pending().mapped = false;
    wf::get_core().tx_manager->schedule_object(wtoplevel);
}

/**
 * A class which manages the xdg_toplevel_view for the duration of the wlr_xdg_toplevel object lifetime.
 */
class xdg_toplevel_controller_t
{
    std::shared_ptr<wf::xdg_toplevel_view_t> view;

    wf::wl_listener_wrapper on_map;
    wf::wl_listener_wrapper on_unmap;
    wf::wl_listener_wrapper on_destroy;

  public:
    xdg_toplevel_controller_t(wlr_xdg_toplevel *toplevel)
    {
        on_destroy.set_callback([=] (auto) { delete this; });
        on_destroy.connect(&toplevel->events.destroy);
        view = wf::xdg_toplevel_view_t::create(toplevel);

        on_map.set_callback([=] (void*)
        {
            wf::view_pre_map_signal pre_map;
            pre_map.view    = view.get();
            pre_map.surface = toplevel->base->surface;
            wf::get_core().emit(&pre_map);

            if (pre_map.override_implementation)
            {
                delete this;
            } else
            {
                view->start_map_tx();
            }
        });

        on_unmap.set_callback([&] (void*)
        {
            view->start_unmap_tx();
        });

        on_map.connect(&toplevel->base->surface->events.map);
        on_unmap.connect(&toplevel->base->surface->events.unmap);
    }

    ~xdg_toplevel_controller_t()
    {}
};

void wf::default_handle_new_xdg_toplevel(wlr_xdg_toplevel *toplevel)
{
    // Will be deleted by the destroy handler
    new xdg_toplevel_controller_t(toplevel);
}
