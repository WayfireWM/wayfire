#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include "wayfire/core.hpp"
#include "../core/core-impl.hpp"
#include "../core/seat/cursor.hpp"
#include <wayfire/view.hpp>
#include <wayfire/nonstd/tracking-allocator.hpp>

#include "wayfire/unstable/wlr-view-events.hpp"
#include "wayfire/util.hpp"
#include "xwayland/xwayland-helpers.hpp"
#include "xwayland/xwayland-view-base.hpp"
#include "xwayland/xwayland-unmanaged-view.hpp"
#include "xwayland/xwayland-toplevel-view.hpp"

#if WF_HAS_XWAYLAND

xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_NORMAL;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_DIALOG;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_SPLASH;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_UTILITY;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_DND;

namespace wf
{
/**
 * A class which manages a xwayland surface for the duration of the wlr_xwayland_surface lifetime.
 */
class xwayland_view_controller_t
{
    std::shared_ptr<wayfire_xwayland_view_internal_base> view;
    wlr_xwayland_surface *xw;

    wf::wl_listener_wrapper on_destroy;
    wf::wl_listener_wrapper on_or_changed;
    wf::wl_listener_wrapper on_set_window_type;
    wf::wl_listener_wrapper on_set_parent;
    wf::wl_listener_wrapper on_map;
    wf::wl_listener_wrapper on_unmap;
    wf::wl_listener_wrapper on_associate;
    wf::wl_listener_wrapper on_dissociate;

  public:
    xwayland_view_controller_t(wlr_xwayland_surface *xsurf)
    {
        this->xw = xsurf;
        on_destroy.set_callback([=] (auto) { delete this; });
        on_destroy.connect(&xw->events.destroy);

        create_view(determine_type());
        on_or_changed.set_callback([&] (void*)
        {
            recreate_view();
        });
        on_set_window_type.set_callback([&] (void*)
        {
            recreate_view();
        });
        on_set_parent.set_callback([&] (void*)
        {
            /* Menus, etc. with TRANSIENT_FOR but not dialogs */
            recreate_view();
        });

        on_or_changed.connect(&xw->events.set_override_redirect);
        on_set_window_type.connect(&xw->events.set_window_type);
        on_set_parent.connect(&xw->events.set_parent);

        on_map.set_callback([&] (void*)
        {
            LOGE("new xwayland surface ", xw->title, " class: ", xw->class_t, " instance: ", xw->instance);
            wf::view_pre_map_signal pre_map;
            pre_map.view    = view.get();
            pre_map.surface = xw->surface;
            wf::get_core().emit(&pre_map);
            if (pre_map.override_implementation)
            {
                delete this;
            } else
            {
                view->handle_map_request(xw->surface);
            }
        });
        on_unmap.set_callback([&] (void*) { view->handle_unmap_request(); });

        on_associate.set_callback([&] (void*)
        {
            on_map.connect(&xw->surface->events.map);
            on_unmap.connect(&xw->surface->events.unmap);
        });
        on_dissociate.set_callback([&] (void*)
        {
            view->priv->wsurface = nullptr;
            on_map.disconnect();
            on_unmap.disconnect();
        });
        on_associate.connect(&xw->events.associate);
        on_dissociate.connect(&xw->events.dissociate);
    }

    ~xwayland_view_controller_t()
    {}

    bool is_dialog()
    {
        if (xw::has_type(xw, wf::xw::_NET_WM_WINDOW_TYPE_DIALOG) ||
            (xw->parent && (xw->window_type_len == 0)))
        {
            return true;
        } else
        {
            return false;
        }
    }

    /**
     * Determine whether the view should be treated as override-redirect or not.
     */
    bool is_unmanaged()
    {
        if (xw->override_redirect)
        {
            return true;
        }

        /** Example: Android Studio dialogs */
        if (xw->parent && !this->is_dialog() &&
            !wf::xw::has_type(xw, wf::xw::_NET_WM_WINDOW_TYPE_NORMAL) &&
            !wf::xw::has_type(xw, wf::xw::_NET_WM_WINDOW_TYPE_UTILITY))
        {
            return true;
        }

        return false;
    }

    /**
     * Determine whether the view should be treated as a drag icon.
     */
    bool is_dnd()
    {
        return wf::xw::has_type(xw, wf::xw::_NET_WM_WINDOW_TYPE_DND);
    }

    wf::xw::view_type determine_type()
    {
        wf::xw::view_type target_type = wf::xw::view_type::NORMAL;
        if (this->is_dnd())
        {
            target_type = wf::xw::view_type::DND;
        } else if (this->is_unmanaged())
        {
            target_type = wf::xw::view_type::UNMANAGED;
        }

        return target_type;
    }

    void create_view(wf::xw::view_type target_type)
    {
        switch (target_type)
        {
          case wf::xw::view_type::DND:
            this->view = wayfire_unmanaged_xwayland_view::create<wayfire_dnd_xwayland_view>(xw);
            break;

          case wf::xw::view_type::UNMANAGED:
            this->view = wayfire_unmanaged_xwayland_view::create<wayfire_unmanaged_xwayland_view>(xw);
            break;

          case wf::xw::view_type::NORMAL:
            this->view = wayfire_xwayland_view::create(xw);
            break;
        }

        if (xw->surface && xw->surface->mapped)
        {
            view->handle_map_request(xw->surface);
        }
    }

    /**
     * Destroy the view, and create a new one with the correct type -
     * unmanaged(override-redirect), DnD or normal.
     *
     * No-op if the view already has the correct type.
     */
    void recreate_view()
    {
        const auto target_type = determine_type();

        if (target_type == view->get_current_impl_type())
        {
            // Nothing changed
            return;
        }

        // destroy the view (unmap + destroy)
        if (view->is_mapped())
        {
            view->handle_unmap_request();
        }

        view->destroy();
        view = nullptr;

        // Create the new view.
        create_view(target_type);
    }
};
}

static wlr_xwayland *xwayland_handle = nullptr;
static wf::wl_listener_wrapper on_xwayland_surface_created;
static wf::wl_listener_wrapper on_xwayland_ready;
#endif

void wf::init_xwayland(bool lazy)
{
#if WF_HAS_XWAYLAND
    on_xwayland_surface_created.set_callback([] (void *data)
    {
        wf::new_xwayland_surface_signal ev;
        ev.surface = (wlr_xwayland_surface*)data;
        wf::get_core().emit(&ev);

        if (ev.use_default_implementation)
        {
            // Will be auto-freed on surface.destroy
            new wf::xwayland_view_controller_t{ev.surface};
        }
    });

    on_xwayland_ready.set_callback([&] (void *data)
    {
        if (!wf::xw::load_basic_atoms(xwayland_handle->display_name))
        {
            LOGE("Failed to load Xwayland atoms.");
        } else
        {
            LOGD("Successfully loaded Xwayland atoms.");
        }

        wlr_xwayland_set_seat(xwayland_handle, wf::get_core().get_current_seat());
        xwayland_update_default_cursor();
    });

    xwayland_handle = wlr_xwayland_create(wf::get_core().display,
        wf::get_core_impl().compositor, lazy);

    if (xwayland_handle)
    {
        on_xwayland_surface_created.connect(&xwayland_handle->events.new_surface);
        on_xwayland_ready.connect(&xwayland_handle->events.ready);
    }

#endif
}

void wf::fini_xwayland()
{
#if WF_HAS_XWAYLAND
    if (xwayland_handle)
    {
        on_xwayland_surface_created.disconnect();
        on_xwayland_ready.disconnect();
        wlr_xwayland_destroy(xwayland_handle);
    }

#endif
}

void wf::xwayland_update_default_cursor()
{
#if WF_HAS_XWAYLAND
    if (!xwayland_handle)
    {
        return;
    }

    auto xc     = wf::get_core_impl().seat->priv->cursor->xcursor;
    auto cursor = wlr_xcursor_manager_get_xcursor(xc, "left_ptr", 1);
    if (cursor && (cursor->image_count > 0))
    {
        auto image = cursor->images[0];
        wlr_xwayland_set_cursor(xwayland_handle, image->buffer,
            image->width * 4, image->width, image->height,
            image->hotspot_x, image->hotspot_y);
    }

#endif
}

void wf::xwayland_bring_to_front(wlr_surface *surface)
{
#if WF_HAS_XWAYLAND

    if (wlr_xwayland_surface *xwayland_surface = wlr_xwayland_surface_try_from_wlr_surface(surface))
    {
        if (!xwayland_surface->override_redirect)
        {
            wlr_xwayland_surface_restack(xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
        }
    }

#endif
}

std::string wf::xwayland_get_display()
{
#if WF_HAS_XWAYLAND

    return xwayland_handle ? nonull(xwayland_handle->display_name) : "";
#else

    return "";
#endif
}

int wf::xwayland_get_pid()
{
#if WF_HAS_XWAYLAND

    return xwayland_handle ? xwayland_handle->server->pid : -1;
#else

    return -1;
#endif
}
