#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include "wayfire/core.hpp"
#include "../core/core-impl.hpp"
#include "../core/seat/cursor.hpp"
#include <wayfire/view.hpp>

#include "wayfire/util.hpp"
#include "xwayland/xwayland-helpers.hpp"
#include "xwayland/xwayland-view-base.hpp"
#include "xwayland/xwayland-unmanaged-view.hpp"
#include "xwayland/xwayland-toplevel-view.hpp"

#if WF_HAS_XWAYLAND

xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_NORMAL;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_DIALOG;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_SPLASH;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_DND;

namespace wf
{
/**
 * A class which manages a xwayland surface for the duration of the wlr_xwayland_surface lifetime.
 */
class xwayland_view_controller_t
{
    nonstd::observer_ptr<wayfire_xwayland_view_base> view_base;
    nonstd::observer_ptr<wf::view_interface_t> view_impl;
    wlr_xwayland_surface *xw;

    wf::wl_listener_wrapper on_destroy;
    wf::wl_listener_wrapper on_or_changed;
    wf::wl_listener_wrapper on_set_window_type;
    wf::wl_listener_wrapper on_set_parent;

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
    }

    ~xwayland_view_controller_t()
    {
        view_impl->unref();
    }

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
            !wf::xw::has_type(xw, wf::xw::_NET_WM_WINDOW_TYPE_NORMAL))
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
        std::unique_ptr<wf::view_interface_t> new_view;
        switch (target_type)
        {
          case wf::xw::view_type::DND:
            new_view = std::make_unique<wayfire_dnd_xwayland_view>(xw);
            break;

          case wf::xw::view_type::UNMANAGED:
            new_view = std::make_unique<wayfire_unmanaged_xwayland_view>(xw);
            break;

          case wf::xw::view_type::NORMAL:
            new_view = std::make_unique<wayfire_xwayland_view>(xw);
            break;
        }

        this->view_base = {dynamic_cast<wayfire_xwayland_view_base*>(new_view.get())};
        this->view_impl = {new_view};
        wf::get_core().add_view(std::move(new_view));
        if (xw->mapped)
        {
            view_base->map(xw->surface);
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

        if (target_type == view_base->get_current_impl_type())
        {
            // Nothing changed
            return;
        }

        // destroy the view (unmap + destroy)
        if (view_impl->is_mapped())
        {
            view_base->unmap();
        }

        view_base->destroy();
        view_impl->unref();
        view_base = nullptr;
        view_impl = nullptr;

        // Create the new view.
        create_view(target_type);
    }
};
}

static wlr_xwayland *xwayland_handle = nullptr;
#endif

void wf::init_xwayland()
{
#if WF_HAS_XWAYLAND
    static wf::wl_listener_wrapper on_created;
    static wf::wl_listener_wrapper on_ready;

    static wf::signal::connection_t<core_shutdown_signal> on_shutdown = [=] (core_shutdown_signal *ev)
    {
        wlr_xwayland_destroy(xwayland_handle);
    };

    on_created.set_callback([] (void *data)
    {
        auto xsurf = (wlr_xwayland_surface*)data;
        new wf::xwayland_view_controller_t{xsurf};
    });

    on_ready.set_callback([&] (void *data)
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
        wf::get_core_impl().compositor, false);

    if (xwayland_handle)
    {
        on_created.connect(&xwayland_handle->events.new_surface);
        on_ready.connect(&xwayland_handle->events.ready);
        wf::get_core().connect(&on_shutdown);
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
    if (wlr_surface_is_xwayland_surface(surface))
    {
        auto xw = wlr_xwayland_surface_from_wlr_surface(surface);
        wlr_xwayland_surface_restack(xw, NULL, XCB_STACK_MODE_ABOVE);
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
