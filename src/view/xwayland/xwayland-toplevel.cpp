#include "xwayland-toplevel.hpp"
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include <wayfire/txn/transaction-manager.hpp>
#include "../view-impl.hpp"

wf::xw::xwayland_toplevel_t::xwayland_toplevel_t(wlr_xwayland_surface *xw)
{
    this->xw = xw;

    on_surface_commit.set_callback([&] (void*) { handle_surface_commit(); });
    on_xw_destroy.set_callback([&] (void*)
    {
        this->xw = NULL;
        on_xw_destroy.disconnect();
        on_surface_commit.disconnect();
        emit_ready();
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
    }

    auto size = get_current_xw_size();

    _pending.geometry.width    = size.width;
    _current.geometry.width    = size.width;
    _committed.geometry.width  = size.width;
    _pending.geometry.height   = size.height;
    _current.geometry.height   = size.height;
    _committed.geometry.height = size.height;
}

void wf::xw::xwayland_toplevel_t::set_output_offset(wf::point_t output_offset)
{
    this->output_offset = output_offset;
    reconfigure_xwayland_surface();
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
    LOGC(TXNI, this, ": committing xwayland state mapped=", _pending.mapped, " geometry=", _pending.geometry);

    if (wf::dimensions(_pending.geometry) == wf::dimensions(_current.geometry))
    {
        emit_ready();
        return;
    }

    if (!this->xw)
    {
        // No longer mapped => we can do whatever
        emit_ready();
        return;
    }

    if (_pending.mapped && !_current.mapped)
    {
        // We are trying to map the toplevel => check whether we should wait until it sets the proper
        // geometry, or whether we are 'only' mapping without resizing.
        if (get_current_xw_size() == wf::dimensions(_pending.geometry))
        {
            emit_ready();
            return;
        }
    }

    reconfigure_xwayland_surface();
    if (main_surface)
    {
        // Send frame done to let the client know it can resize
        main_surface->send_frame_done();
    }
}

void wf::xw::xwayland_toplevel_t::reconfigure_xwayland_surface()
{
    if (!xw)
    {
        return;
    }

    auto margins = get_margins();
    const int configure_x     = _pending.geometry.x - margins.left + output_offset.x;
    const int configure_y     = _pending.geometry.y - margins.top + output_offset.y;
    const int configure_width = std::max(1, _pending.geometry.width - margins.left - margins.right);
    const int configure_height = std::max(1, _pending.geometry.height - margins.top - margins.bottom);

    if ((configure_width < 0) || (configure_height < 0))
    {
        /* such a configure request would freeze xwayland.
         * This is most probably a bug somewhere in the compositor. */
        LOGE("Configuring a xwayland surface with width/height <0");
        return;
    }

    wlr_xwayland_surface_configure(xw, configure_x, configure_y, configure_width, configure_height);
}

void wf::xw::xwayland_toplevel_t::apply()
{
    xwayland_toplevel_applied_state_signal event_applied;
    event_applied.old_state = current();

    if (!xw)
    {
        // If toplevel does no longer exist, we can't change the size anymore.
        _committed.geometry.width  = _current.geometry.width;
        _committed.geometry.height = _current.geometry.height;
    }

    wf::adjust_geometry_for_gravity(_committed, this->get_current_xw_size());
    this->_current = committed();
    apply_pending_state();
    reconfigure_xwayland_surface();

    emit(&event_applied);
}

void wf::xw::xwayland_toplevel_t::handle_surface_commit()
{
    pending_state.merge_state(main_surface->get_surface());

    const bool is_committed = wf::get_core().tx_manager->is_object_committed(shared_from_this());
    if (is_committed)
    {
        if (get_current_xw_size() != wf::dimensions(_committed.geometry))
        {
            // Desired state not reached => wait for the desired state to be reached. In the meantime, send a
            // frame done so that the client can redraw faster.
            main_surface->send_frame_done();
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

    auto toplevel_size = get_current_xw_size();
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

void wf::xw::xwayland_toplevel_t::set_decoration(decorator_frame_t_t *frame)
{
    this->frame = frame;
}

wf::geometry_t wf::xw::xwayland_toplevel_t::calculate_base_geometry()
{
    if (!main_surface)
    {
        return {0, 0, 0, 0};
    }

    auto geometry = current().geometry;
    auto margins  = get_margins();
    geometry.x     = geometry.x + margins.left;
    geometry.y     = geometry.y + margins.top;
    geometry.width = main_surface->get_bounding_box().width;
    geometry.height = main_surface->get_bounding_box().height;
    return geometry;
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

wf::decoration_margins_t wf::xw::xwayland_toplevel_t::get_margins()
{
    return frame ? frame->get_margins() : wf::decoration_margins_t{0, 0, 0, 0};
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
    auto margins = get_margins();
    size.width  += margins.left + margins.right;
    size.height += margins.top + margins.bottom;
    return size;
}
