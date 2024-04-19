#pragma once

#include "config.h"
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/signal-definitions.hpp>

#include "wayfire/util.hpp"
#include "xwayland-helpers.hpp"
#include <wayfire/scene-operations.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/unstable/xwl-toplevel-base.hpp>

#if WF_HAS_XWAYLAND

class wayfire_xwayland_view_internal_base : public wf::xwayland_view_base_t
{
  protected:
    wf::wl_listener_wrapper on_configure;
    wf::wl_listener_wrapper on_surface_commit;

    /** The geometry requested by the client */
    bool self_positioned = false;

  public:
    wayfire_xwayland_view_internal_base(wlr_xwayland_surface *xww) : xwayland_view_base_t(xww)
    {}

    /**
     * Get the current implementation type.
     */
    virtual wf::xw::view_type get_current_impl_type() const = 0;

    virtual ~wayfire_xwayland_view_internal_base() = default;

    void _initialize()
    {
        on_configure.set_callback([&] (void *data)
        {
            handle_client_configure((wlr_xwayland_surface_configure_event*)data);
        });

        on_configure.connect(&xw->events.request_configure);
    }

    virtual void handle_client_configure(wlr_xwayland_surface_configure_event *ev)
    {}

    virtual void handle_dissociate()
    {}

    void destroy() override
    {
        wf::xwayland_view_base_t::destroy();
        on_configure.disconnect();
    }

    virtual void handle_map_request(wlr_surface *surface) = 0;
    virtual void handle_unmap_request() = 0;
};

#endif
