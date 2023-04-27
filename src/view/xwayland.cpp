#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include "wayfire/core.hpp"
#include "../core/core-impl.hpp"
#include "../core/seat/cursor.hpp"

#include "xwayland/xwayland-helpers.hpp"
#include "xwayland/xwayland-view-base.hpp"
#include "xwayland/xwayland-unmanaged-view.hpp"
#include "xwayland/xwayland-toplevel-view.hpp"

#if WF_HAS_XWAYLAND

xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_NORMAL;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_DIALOG;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_SPLASH;
xcb_atom_t wf::xw::_NET_WM_WINDOW_TYPE_DND;

void wayfire_xwayland_view_base::recreate_view()
{
    wf::xw::view_type target_type = wf::xw::view_type::NORMAL;
    if (this->is_dnd())
    {
        target_type = wf::xw::view_type::DND;
    } else if (this->is_unmanaged())
    {
        target_type = wf::xw::view_type::UNMANAGED;
    }

    if (target_type == this->get_current_impl_type())
    {
        // Nothing changed
        return;
    }

    /*
     * Copy xw and mapped status into the stack, because "this" may be destroyed
     * at some point of this function.
     */
    auto xw_surf    = this->xw;
    bool was_mapped = is_mapped();

    // destroy the view (unmap + destroy)
    if (was_mapped)
    {
        unmap();
    }

    destroy();

    // Create the new view.
    // Take care! The new_view pointer is passed to core as unique_ptr
    wayfire_xwayland_view_base *new_view;
    switch (target_type)
    {
      case wf::xw::view_type::DND:
        new_view = new wayfire_dnd_xwayland_view(xw_surf);
        break;

      case wf::xw::view_type::UNMANAGED:
        new_view = new wayfire_unmanaged_xwayland_view(xw_surf);
        break;

      case wf::xw::view_type::NORMAL:
        new_view = new wayfire_xwayland_view(xw_surf);
        break;
    }

    wf::get_core().add_view(std::unique_ptr<view_interface_t>(new_view));
    if (was_mapped)
    {
        new_view->map(xw_surf->surface);
    }
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
        if (xsurf->override_redirect)
        {
            wf::get_core().add_view(
                std::make_unique<wayfire_unmanaged_xwayland_view>(xsurf));
        } else
        {
            wf::get_core().add_view(
                std::make_unique<wayfire_xwayland_view>(xsurf));
        }
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

        wlr_xwayland_set_seat(xwayland_handle,
            wf::get_core().get_current_seat());
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
