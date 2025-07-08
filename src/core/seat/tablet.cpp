#include <wayfire/bindings-repository.hpp>
#include "tablet.hpp"
#include "../core-impl.hpp"
#include "pointer.hpp"
#include "cursor.hpp"
#include "view/view-impl.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/view.hpp"
#include <algorithm>
#include <memory>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output-layout.hpp>
#include <linux/input-event-codes.h>
#include <wayfire/window-manager.hpp>

/* --------------------- Tablet tool implementation ------------------------- */
wf::tablet_tool_t::tablet_tool_t(wlr_tablet_tool *tool,
    wlr_tablet_v2_tablet *tablet_v2)
{
    this->tablet_v2 = tablet_v2;

    /* Initialize tool_v2 */
    this->tool = tool;
    this->tool->data = this;

    auto& core = wf::get_core_impl();
    this->tool_v2 = wlr_tablet_tool_create(core.protocols.tablet_v2,
        core.get_current_seat(), tool);

    /* Free memory when the tool is destroyed */
    this->on_destroy.set_callback([=] (void*)
    {
        this->tool->data = nullptr;
        tablet->tools_list.erase(
            std::remove_if(tablet->tools_list.begin(), tablet->tools_list.end(),
                [this] (const auto& p) { return p.get() == this; }),
            tablet->tools_list.end());
    });
    this->on_destroy.connect(&tool->events.destroy);

    /* Ungrab surface, and update focused surface if a surface is unmapped,
     * we don't want to end up with a reference to unfocused or a destroyed
     * surface. */
    on_root_node_updated = [=] (wf::scene::root_node_update_signal *data)
    {
        if (!(data->flags & wf::scene::update_flag::INPUT_STATE))
        {
            return;
        }

        if (grabbed_node && !is_grabbed_node_alive(grabbed_node))
        {
            reset_grab();
        }

        update_tool_position(false);
    };

    wf::get_core().scene()->connect(&on_root_node_updated);

    /* Just pass cursor set requests to core, but translate them to
     * regular pointer set requests */
    on_set_cursor.set_callback([=] (void *data)
    {
        if (!this->is_active)
        {
            return;
        }

        auto ev = static_cast<wlr_tablet_v2_event_cursor*>(data);
        // validate request
        wlr_seat_client *tablet_client = nullptr;
        if (tool_v2->focused_surface)
        {
            tablet_client = wlr_seat_client_for_wl_client(
                wf::get_core().get_current_seat(),
                wl_resource_get_client(tool_v2->focused_surface->resource));
        }

        if (tablet_client != ev->seat_client)
        {
            return;
        }

        wlr_seat_pointer_request_set_cursor_event pev;
        pev.surface   = ev->surface;
        pev.hotspot_x = ev->hotspot_x;
        pev.hotspot_y = ev->hotspot_y;
        pev.serial    = ev->serial;
        pev.seat_client = ev->seat_client;
        wf::get_core_impl().seat->priv->cursor->set_cursor(&pev, false);
    });
    on_set_cursor.connect(&tool_v2->events.set_cursor);
}

wf::tablet_tool_t::~tablet_tool_t()
{
    tool->data = NULL;
}

static inline wlr_surface *wlr_surface_from_node(wf::scene::node_ptr node)
{
    if (auto snode = dynamic_cast<wf::scene::wlr_surface_node_t*>(node.get()))
    {
        return snode->get_surface();
    }

    return nullptr;
}

void wf::tablet_tool_t::update_tool_position(bool real_update)
{
    if (!is_active)
    {
        return;
    }

    auto& core = wf::get_core_impl();
    auto gc    = core.get_cursor_position();

    /* Figure out what surface is under the tool */
    wf::pointf_t local; // local to the surface
    wf::scene::node_ptr focus_node = nullptr;
    if (this->grabbed_node)
    {
        focus_node = this->grabbed_node;
        local = get_node_local_coords(focus_node.get(), gc);
    } else
    {
        auto input_node = wf::get_core().scene()->find_node_at(gc);
        if (input_node)
        {
            focus_node = input_node->node->shared_from_this();
            local = input_node->local_coords;
        }
    }

    bool focus_changed = set_focus(focus_node);

    /* If focus is a wlr surface, send position */
    wlr_surface *next_focus = wlr_surface_from_node(focus_node);
    if (next_focus && (real_update || focus_changed))
    {
        wlr_tablet_v2_tablet_tool_notify_motion(tool_v2, local.x, local.y);
    }
}

bool wf::tablet_tool_t::set_focus(wf::scene::node_ptr surface)
{
    bool focus_changed = surface != this->proximity_surface;

    /* Unfocus old surface */
    if ((surface != this->proximity_surface) && this->proximity_surface)
    {
        wlr_tablet_v2_tablet_tool_notify_proximity_out(tool_v2);
        this->proximity_surface = nullptr;
    }

    /* Set the new focus, if it is a wlr surface */

    wlr_surface *next_focus = wlr_surface_from_node(surface);
    if (next_focus)
    {
        wf::xwayland_bring_to_front(next_focus);
    }

    if (next_focus && wlr_surface_accepts_tablet_v2(next_focus, tablet_v2))
    {
        this->proximity_surface = surface;
        wlr_tablet_v2_tablet_tool_notify_proximity_in(tool_v2, tablet_v2, next_focus);
    }

    if (!next_focus)
    {
        wf::get_core().set_cursor("default");
    }

    return focus_changed;
}

void wf::tablet_tool_t::reset_grab()
{
    this->grabbed_node = nullptr;
}

void wf::tablet_tool_t::passthrough_axis(wlr_tablet_tool_axis_event *ev)
{
    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE)
    {
        wlr_tablet_v2_tablet_tool_notify_pressure(tool_v2, ev->pressure);
    }

    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE)
    {
        wlr_tablet_v2_tablet_tool_notify_distance(tool_v2, ev->distance);
    }

    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION)
    {
        wlr_tablet_v2_tablet_tool_notify_rotation(tool_v2, ev->rotation);
    }

    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER)
    {
        wlr_tablet_v2_tablet_tool_notify_slider(tool_v2, ev->slider);
    }

    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL)
    {
        wlr_tablet_v2_tablet_tool_notify_wheel(tool_v2, ev->wheel_delta, 0);
    }

    /* Update tilt, use old values if no new values are provided */
    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_X)
    {
        tilt_x = ev->tilt_x;
    }

    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_Y)
    {
        tilt_y = ev->tilt_y;
    }

    if (ev->updated_axes &
        (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y))
    {
        wlr_tablet_v2_tablet_tool_notify_tilt(tool_v2, tilt_x, tilt_y);
    }
}

void wf::tablet_tool_t::handle_tip(wlr_tablet_tool_tip_event *ev)
{
    /* Nothing to do without a proximity surface */
    if (!this->proximity_surface)
    {
        return;
    }

    if (ev->state == WLR_TABLET_TOOL_TIP_DOWN)
    {
        wlr_send_tablet_v2_tablet_tool_down(tool_v2);
        this->grabbed_node = this->proximity_surface;

        /* Try to focus the view under the tool */
        auto view = wf::node_to_view(this->proximity_surface);
        wf::get_core().default_wm->focus_raise_view(view);
    } else
    {
        wlr_send_tablet_v2_tablet_tool_up(tool_v2);
        this->grabbed_node = nullptr;
        update_tool_position(false);
    }
}

void wf::tablet_tool_t::handle_button(wlr_tablet_tool_button_event *ev)
{
    wlr_tablet_v2_tablet_tool_notify_button(tool_v2,
        (zwp_tablet_pad_v2_button_state)ev->button,
        (zwp_tablet_pad_v2_button_state)ev->state);
}

void wf::tablet_tool_t::handle_proximity(wlr_tablet_tool_proximity_event *ev)
{
    if (ev->state == WLR_TABLET_TOOL_PROXIMITY_OUT)
    {
        set_focus(nullptr);
        is_active = false;
    } else
    {
        is_active = true;
        update_tool_position(true);
    }
}

/* ----------------------- Tablet implementation ---------------------------- */
wf::tablet_t::tablet_t(wlr_cursor *cursor, wlr_input_device *dev) :
    input_device_impl_t(dev)
{
    this->handle = wlr_tablet_from_input_device(dev);
    this->handle->data = this;
    this->cursor = cursor;

    auto& core = wf::get_core_impl();
    tablet_v2 = wlr_tablet_create(core.protocols.tablet_v2,
        core.get_current_seat(), dev);
}

wf::tablet_t::~tablet_t()
{
    this->handle->data = NULL;
}

wf::tablet_tool_t*wf::tablet_t::ensure_tool(wlr_tablet_tool *tool)
{
    if (tool->data == NULL)
    {
        auto wtool = std::make_unique<wf::tablet_tool_t>(tool, tablet_v2);
        wtool->tablet = this;
        auto ptr = wtool.get();
        this->tools_list.push_back(std::move(wtool));
        return ptr;
    }

    return (wf::tablet_tool_t*)tool->data;
}

void wf::tablet_t::handle_tip(wlr_tablet_tool_tip_event *ev,
    input_event_processing_mode_t mode)
{
    if (should_use_absolute_positioning(ev->tool))
    {
        wlr_cursor_warp_absolute(cursor, &ev->tablet->base, ev->x, ev->y);
    }

    auto& seat = wf::get_core_impl().seat;
    seat->priv->break_mod_bindings();

    bool handled_in_binding = false;
    if (ev->state == WLR_TABLET_TOOL_TIP_DOWN)
    {
        auto gc     = seat->priv->cursor->get_cursor_position();
        auto output =
            wf::get_core().output_layout->get_output_at(gc.x, gc.y);
        wf::get_core().seat->focus_output(output);

        handled_in_binding |= wf::get_core().bindings->handle_button(
            wf::buttonbinding_t{seat->priv->get_modifiers(), BTN_LEFT});
    }

    auto tool = ensure_tool(ev->tool);
    if (!handled_in_binding)
    {
        tool->handle_tip(ev);
    }
}

void wf::tablet_t::handle_axis(wlr_tablet_tool_axis_event *ev,
    input_event_processing_mode_t mode)
{
    /* Update cursor position */
    if (should_use_absolute_positioning(ev->tool))
    {
        double x = (ev->updated_axes & WLR_TABLET_TOOL_AXIS_X) ? ev->x : NAN;
        double y = (ev->updated_axes & WLR_TABLET_TOOL_AXIS_Y) ? ev->y : NAN;
        wlr_cursor_warp_absolute(cursor, &ev->tablet->base, x, y);
    } else
    {
        wlr_cursor_move(cursor, &ev->tablet->base, ev->dx, ev->dy);
    }

    /* Update focus */
    auto tool = ensure_tool(ev->tool);
    tool->update_tool_position(true);
    tool->passthrough_axis(ev);
}

void wf::tablet_t::handle_button(wlr_tablet_tool_button_event *ev,
    input_event_processing_mode_t mode)
{
    /* Pass to the tool */
    ensure_tool(ev->tool)->handle_button(ev);
}

void wf::tablet_t::handle_proximity(wlr_tablet_tool_proximity_event *ev,
    input_event_processing_mode_t mode)
{
    if (should_use_absolute_positioning(ev->tool))
    {
        wlr_cursor_warp_absolute(cursor, &ev->tablet->base, ev->x, ev->y);
    }

    ensure_tool(ev->tool)->handle_proximity(ev);
    auto& impl = wf::get_core_impl();

    /* Show appropriate cursor */
    if (ev->state == WLR_TABLET_TOOL_PROXIMITY_OUT)
    {
        impl.set_cursor("default");
        impl.seat->priv->lpointer->set_enable_focus(true);
    } else
    {
        wf::get_core().set_cursor("crosshair");
        impl.seat->priv->lpointer->set_enable_focus(false);
    }
}

/* ------------------------ Tablet pad implementation ----------------------- */
wf::tablet_pad_t::tablet_pad_t(wlr_input_device *pad) :
    input_device_impl_t(pad)
{
    auto& core = wf::get_core();
    this->pad_v2 = wlr_tablet_pad_create(core.protocols.tablet_v2,
        core.get_current_seat(), pad);


    on_device_added = [=] (auto)
    {
        select_default_tool();
    };

    on_device_removed = [=] (auto)
    {
        select_default_tool();
    };

    wf::get_core().connect(&on_device_added);
    wf::get_core().connect(&on_device_removed);

    on_keyboard_focus_changed.set_callback([=] (auto)
    {
        update_focus();
    });
    wf::get_core().connect(&on_keyboard_focus_changed);
    select_default_tool();

    on_attach.set_callback([=] (void *data)
    {
        auto wlr_tool = static_cast<wlr_tablet_tool*>(data);
        auto tool     = (tablet_tool_t*)wlr_tool->data;
        if (tool)
        {
            attach_to_tablet(tool->tablet);
        }
    });

    on_button.set_callback([=] (void *data)
    {
        auto ev = static_cast<wlr_tablet_pad_button_event*>(data);
        wlr_tablet_v2_tablet_pad_notify_mode(pad_v2,
            ev->group, ev->mode, ev->time_msec);
        wlr_tablet_v2_tablet_pad_notify_button(pad_v2,
            ev->button, ev->time_msec,
            (zwp_tablet_pad_v2_button_state)ev->state);
    });

    on_strip.set_callback([=] (void *data)
    {
        auto ev = static_cast<wlr_tablet_pad_strip_event*>(data);
        wlr_tablet_v2_tablet_pad_notify_strip(pad_v2, ev->strip, ev->position,
            ev->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER, ev->time_msec);
    });

    on_ring.set_callback([=] (void *data)
    {
        auto ev = static_cast<wlr_tablet_pad_ring_event*>(data);
        wlr_tablet_v2_tablet_pad_notify_ring(pad_v2, ev->ring, ev->position,
            ev->source == WLR_TABLET_PAD_RING_SOURCE_FINGER, ev->time_msec);
    });

    on_attach.connect(&wlr_tablet_pad_from_input_device(pad)->events.attach_tablet);
    on_button.connect(&wlr_tablet_pad_from_input_device(pad)->events.button);
    on_strip.connect(&wlr_tablet_pad_from_input_device(pad)->events.strip);
    on_ring.connect(&wlr_tablet_pad_from_input_device(pad)->events.ring);

    on_focus_destroy.set_callback([&] (auto) { update_focus(nullptr); });
}

void wf::tablet_pad_t::update_focus()
{
    auto focus_view    = wf::get_core().seat->get_active_view();
    auto focus_surface = focus_view ? focus_view->get_wlr_surface() : nullptr;
    update_focus(focus_surface);
}

void wf::tablet_pad_t::update_focus(wlr_surface *focus_surface)
{
    if (focus_surface == old_focus)
    {
        return;
    }

    if (old_focus)
    {
        wlr_tablet_v2_tablet_pad_notify_leave(pad_v2, old_focus);
    }

    if (focus_surface && attached_to)
    {
        wlr_tablet_v2_tablet_pad_notify_enter(pad_v2, attached_to->tablet_v2, focus_surface);
    }

    on_focus_destroy.disconnect();
    if (focus_surface)
    {
        on_focus_destroy.connect(&focus_surface->events.destroy);
    }

    old_focus = focus_surface;
}

void wf::tablet_pad_t::attach_to_tablet(tablet_t *tablet)
{
    update_focus(nullptr);
    this->attached_to = nonstd::make_observer(tablet);
    update_focus();
}

static libinput_device_group *get_group(wlr_input_device *dev)
{
    if (wlr_input_device_is_libinput(dev))
    {
        auto hnd = wlr_libinput_get_device_handle(dev);
        return libinput_device_get_device_group(hnd);
    }

    return nullptr;
}

void wf::tablet_pad_t::select_default_tool()
{
    auto devices = wf::get_core().get_input_devices();
    for (auto& dev : devices)
    {
        /* Remain as-is */
        if (dev == attached_to)
        {
            return;
        }

        if (dev->get_wlr_handle()->type != WLR_INPUT_DEVICE_TABLET)
        {
            continue;
        }

        auto pad_gr = get_group(this->get_wlr_handle());
        auto tab_gr = get_group(dev->get_wlr_handle());

        if (pad_gr == tab_gr)
        {
            attach_to_tablet(static_cast<tablet_t*>(dev.get()));
            return;
        }
    }

    attach_to_tablet(nullptr);
}

bool wf::tablet_t::should_use_absolute_positioning(wlr_tablet_tool *tool)
{
    static wf::option_wrapper_t<std::string> tablet_motion_mode{"input/tablet_motion_mode"};

    /* Update cursor position */
    if ((std::string)tablet_motion_mode == "absolute")
    {
        return true;
    } else if ((std::string)tablet_motion_mode == "relative")
    {
        return false;
    } else
    {
        return tool->type != WLR_TABLET_TOOL_TYPE_MOUSE;
    }
}
