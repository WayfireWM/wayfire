#pragma once

#include "config.h"
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/signal-definitions.hpp>

#include "../view-impl.hpp"
#include "../toplevel-node.hpp"
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/view.hpp"
#include "xwayland-helpers.hpp"
#include <wayfire/scene-operations.hpp>
#include <wayfire/view-helpers.hpp>

#if WF_HAS_XWAYLAND

class wayfire_xwayland_view_base
{
  protected:
    wf::wl_listener_wrapper on_destroy, on_configure, on_set_title, on_set_app_id, on_ping_timeout;
    wf::wl_listener_wrapper on_surface_commit;
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;

    wlr_xwayland_surface *xw;
    /** The geometry requested by the client */
    bool self_positioned = false;

    std::string title, app_id;
    /** Used by view implementations when the app id changes */
    void handle_app_id_changed(std::string new_app_id)
    {
        this->app_id = new_app_id;
        wf::view_implementation::emit_app_id_changed_signal(dynamic_cast<wf::view_interface_t*>(this));
    }

    /** Used by view implementations when the title changes */
    void handle_title_changed(std::string new_title)
    {
        this->title = new_title;
        wf::view_implementation::emit_title_changed_signal(dynamic_cast<wf::view_interface_t*>(this));
    }

  public:
    wayfire_xwayland_view_base(wlr_xwayland_surface *xww) : xw(xww)
    {}

    /**
     * Get the current implementation type.
     */
    virtual wf::xw::view_type get_current_impl_type() const = 0;

    virtual ~wayfire_xwayland_view_base() = default;

    void _initialize()
    {
        on_destroy.set_callback([&] (void*) { destroy(); });
        on_configure.set_callback([&] (void *data)
        {
            handle_client_configure((wlr_xwayland_surface_configure_event*)data);
        });
        on_set_title.set_callback([&] (void*)
        {
            handle_title_changed(nonull(xw->title));
        });
        on_set_app_id.set_callback([&] (void*)
        {
            handle_app_id_changed(nonull(xw->class_t));
        });
        on_ping_timeout.set_callback([&] (void*)
        {
            wf::view_implementation::emit_ping_timeout_signal(dynamic_cast<wf::view_interface_t*>(this));
        });
        handle_title_changed(nonull(xw->title));
        handle_app_id_changed(nonull(xw->class_t));

        on_destroy.connect(&xw->events.destroy);
        on_configure.connect(&xw->events.request_configure);
        on_set_title.connect(&xw->events.set_title);
        on_set_app_id.connect(&xw->events.set_class);
        on_ping_timeout.connect(&xw->events.ping_timeout);
    }

    virtual void handle_client_configure(wlr_xwayland_surface_configure_event *ev)
    {}

    virtual void destroy()
    {
        this->xw = nullptr;

        on_destroy.disconnect();
        on_configure.disconnect();
        on_set_title.disconnect();
        on_set_app_id.disconnect();
        on_ping_timeout.disconnect();
    }

    void _ping()
    {
        if (xw)
        {
            wlr_xwayland_surface_ping(xw);
        }
    }

    void _close()
    {
        if (xw)
        {
            wlr_xwayland_surface_close(xw);
        }
    }

    virtual void map(wlr_surface *surface) = 0;
    virtual void unmap() = 0;
};

#endif
