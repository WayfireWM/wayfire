#include "xwayland-toplevel.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include <wayfire/txn/transaction-manager.hpp>
#include "../view-impl.hpp"
#include "wayfire/toplevel.hpp"

#if WF_HAS_XWAYLAND

wf::xw::xwayland_toplevel_t::xwayland_toplevel_t(wlr_xwayland_surface *xw)
{
    this->xw = xw;

    on_surface_commit.set_callback([&] (void*) { handle_surface_commit(); });
    on_xw_destroy.set_callback([&] (void*)
    {
        this->xw = NULL;
        on_xw_destroy.disconnect();
        on_surface_commit.disconnect();

        // Emit the ready signal on the next idle, to give all substructures time to properly deinitialize.
        idle_ready.run_once([&] () { emit_ready(); });
    });

    on_xw_destroy.connect(&xw->events.destroy);
}

void wf::xw::xwayland_toplevel_t::set_main_surface(
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface)
{
    this->main_surface = main_surface;
    on_surface_commit.disconnect();

    if (main_surface)
    {
        if (main_surface->get_surface())
        {
            on_surface_commit.connect(&main_surface->get_surface()->events.commit);
        } else
        {
            LOGW("Setting xwayland toplevel's main surface to a surface without wlr_surface!");
            return;
        }

        auto size = expand_dimensions_by_margins(get_current_xw_size(), _current.margins);
        _pending.geometry.width    = size.width;
        _current.geometry.width    = size.width;
        _committed.geometry.width  = size.width;
        _pending.geometry.height   = size.height;
        _current.geometry.height   = size.height;
        _committed.geometry.height = size.height;
    }
}

void wf::xw::xwayland_toplevel_t::set_output_offset(wf::point_t output_offset)
{
    this->output_offset = output_offset;
    if (pending().mapped)
    {
        // We want to reconfigure xwayland surfaces with output changes only if they are mapped.
        // Otherwise, there is no need to generate x11 events, not to mention that perhaps we do not know the
        // position of the view yet (e.g. if it had never been mapped so far).
        reconfigure_xwayland_surface();
    }
}

void wf::xw::xwayland_toplevel_t::request_native_size()
{
    if (!xw || !xw->size_hints)
    {
        return;
    }

    if ((xw->size_hints->base_width > 0) && (xw->size_hints->base_height > 0))
    {
        this->pending().geometry.width  = xw->size_hints->base_width;
        this->pending().geometry.height = xw->size_hints->base_height;
        wf::get_core().tx_manager->schedule_object(this->shared_from_this());
    }
}

void wf::xw::xwayland_toplevel_t::commit()
{
    this->pending_ready = true;
    _committed = _pending;
    LOGC(TXNI, this, ": committing xwayland state mapped=", _pending.mapped, " geometry=", _pending.geometry,
        " tiled=", _pending.tiled_edges, " fs=", _pending.fullscreen,
        " margins=", _pending.margins.left, ",", _pending.margins.right, ",",
        _pending.margins.top, ",", _pending.margins.bottom);

    if (!this->xw)
    {
        // No longer mapped => we can do whatever
        emit_ready();
        return;
    }

    wf::dimensions_t current_size =
        shrink_dimensions_by_margins(wf::dimensions(_current.geometry), _current.margins);
    if (_pending.mapped && !_current.mapped)
    {
        // We are trying to map the toplevel => check whether we should wait until it sets the proper
        // geometry, or whether we are 'only' mapping without resizing.
        current_size = get_current_xw_size();
    }

    const wf::dimensions_t desired_size = wf::shrink_dimensions_by_margins(
        wf::dimensions(_pending.geometry), _pending.margins);

    bool wait_for_client = false;
    if (desired_size != current_size)
    {
        wait_for_client = true;
        reconfigure_xwayland_surface();
    }

    if (_pending.tiled_edges != _current.tiled_edges)
    {
        wait_for_client = true;
        wlr_xwayland_surface_set_maximized(xw, !!_pending.tiled_edges);
    }

    if (_pending.fullscreen != _current.fullscreen)
    {
        wait_for_client = true;
        wlr_xwayland_surface_set_fullscreen(xw, _pending.fullscreen);
    }

    if (wait_for_client && main_surface)
    {
        // Send frame done to let the client know it can resize
        main_surface->send_frame_done(true);
    } else
    {
        emit_ready();
    }
}

void wf::xw::xwayland_toplevel_t::reconfigure_xwayland_surface()
{
    if (!xw)
    {
        return;
    }

    const wf::geometry_t configure =
        shrink_geometry_by_margins(_pending.geometry, _pending.margins) + output_offset;

    if ((configure.width <= 0) || (configure.height <= 0))
    {
        /* such a configure request would freeze xwayland.
         * This is most probably a bug somewhere in the compositor. */
        LOGE("Configuring a xwayland surface with width/height <0");
        return;
    }

    LOGC(XWL, "Configuring xwayland surface ", nonull(xw->title), " ", nonull(xw->class_t), " ", configure);
    wlr_xwayland_surface_configure(xw, configure.x, configure.y, configure.width, configure.height);
}

void wf::xw::xwayland_toplevel_t::apply()
{
    xwayland_toplevel_applied_state_signal event_applied;
    event_applied.old_state = current();

    // Damage the main surface before applying the new state. This ensures that the old position of the view
    // is damaged.
    if (main_surface && main_surface->parent())
    {
        wf::scene::damage_node(main_surface->parent(), main_surface->parent()->get_bounding_box());
    }

    if (!xw)
    {
        // If toplevel does no longer exist, we can't change the size anymore.
        _committed.geometry.width  = _current.geometry.width;
        _committed.geometry.height = _current.geometry.height;
        if (_current.mapped == false)
        {
            // Avoid mapping if the view was already destroyed.
            _committed.mapped = false;
        }
    }

    if (main_surface && main_surface->get_surface())
    {
        wf::adjust_geometry_for_gravity(_committed,
            expand_dimensions_by_margins(this->get_current_xw_size(), _committed.margins));
    }

    this->_current = committed();
    const bool is_pending = wf::get_core().tx_manager->is_object_pending(shared_from_this());
    if (!is_pending)
    {
        // Adjust for potential moves due to gravity
        _pending = committed();
        reconfigure_xwayland_surface();
    }

    apply_pending_state();
    emit(&event_applied);

    // Damage the new position.
    if (main_surface && main_surface->parent())
    {
        wf::scene::damage_node(main_surface->parent(), main_surface->parent()->get_bounding_box());
    }
}

void wf::xw::xwayland_toplevel_t::handle_surface_commit()
{
    pending_state.merge_state(main_surface->get_surface());

    const bool is_committed = wf::get_core().tx_manager->is_object_committed(shared_from_this());
    if (is_committed)
    {
        const wf::dimensions_t desired_size =
            shrink_dimensions_by_margins(wf::dimensions(_committed.geometry), _committed.margins);

        if (get_current_xw_size() != desired_size)
        {
            // Desired state not reached => wait for the desired state to be reached. In the meantime, send a
            // frame done so that the client can redraw faster.
            main_surface->send_frame_done(true);
            return;
        }

        adjust_geometry_for_gravity(_committed, this->get_current_xw_size());
        emit_ready();
        return;
    }

    const bool is_pending = wf::get_core().tx_manager->is_object_pending(shared_from_this());
    if (is_pending)
    {
        return;
    }

    auto toplevel_size = expand_dimensions_by_margins(get_current_xw_size(), current().margins);
    if (toplevel_size == wf::dimensions(current().geometry))
    {
        // Size did not change, there are no transactions going on - apply the new texture directly
        apply_pending_state();
        return;
    }

    adjust_geometry_for_gravity(_pending, toplevel_size);
    LOGC(VIEWS, "Client-initiated resize to geometry ", pending().geometry);
    auto tx = wf::txn::transaction_t::create();
    tx->add_object(shared_from_this());
    wf::get_core().tx_manager->schedule_transaction(std::move(tx));
}

wf::geometry_t wf::xw::xwayland_toplevel_t::calculate_base_geometry()
{
    auto geometry = current().geometry;
    return shrink_geometry_by_margins(geometry, _current.margins);
}

void wf::xw::xwayland_toplevel_t::apply_pending_state()
{
    if (xw && xw->surface)
    {
        pending_state.merge_state(xw->surface);
    }

    if (main_surface)
    {
        main_surface->apply_state(std::move(pending_state));
    }
}

void wf::xw::xwayland_toplevel_t::emit_ready()
{
    if (pending_ready)
    {
        pending_ready = false;
        emit_object_ready(this);
    }
}

wf::dimensions_t wf::xw::xwayland_toplevel_t::get_current_xw_size()
{
    if (!main_surface || !main_surface->get_surface())
    {
        return {0, 0};
    }

    auto surf = main_surface->get_surface();
    wf::dimensions_t size = wf::dimensions_t{surf->current.width, surf->current.height};
    return size;
}

wf::dimensions_t wf::xw::xwayland_toplevel_t::get_min_size()
{
    if (xw && xw->size_hints)
    {
        return wf::dimensions_t{
            std::max(0, xw->size_hints->min_width),
            std::max(0, xw->size_hints->min_height)
        };
    }

    return {0, 0};
}

wf::dimensions_t wf::xw::xwayland_toplevel_t::get_max_size()
{
    if (xw && xw->size_hints)
    {
        return wf::dimensions_t{
            std::max(0, xw->size_hints->max_width),
            std::max(0, xw->size_hints->max_height)
        };
    }

    return {0, 0};
}

#endif
