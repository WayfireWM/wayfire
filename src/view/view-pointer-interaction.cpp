#include <wayfire/view.hpp>
#include <wayfire/scene.hpp>
#include "../core/seat/surface-map-state.hpp"
#include "../core/seat/input-manager.hpp"
#include "../core/core-impl.hpp"
#include <wayfire/compositor-surface.hpp>
#include "view-impl.hpp"

class view_pointer_interaction_t final : public wf::pointer_interaction_t
{
    wayfire_view view;
    wf::surface_interface_t *last_surface = NULL;
    wlr_pointer_constraint_v1 *last_constraint = NULL;

    int nr_held_buttons = 0;
    wf::SurfaceMapStateListener on_surface_map_state_change;

    wf::wl_listener_wrapper constraint_destroyed;

    // From position relative to current focus to global scene coordinates
    wf::pointf_t get_absolute_position_from_relative(wf::pointf_t relative)
    {
        auto output_geometry = view->get_output_geometry();
        wf::point_t origin   = {output_geometry.x, output_geometry.y};

        for (auto& surf : view->enumerate_surfaces(origin))
        {
            if (surf.surface == last_surface)
            {
                relative.x += surf.position.x;
                relative.y += surf.position.y;
            }
        }

        relative = view->transform_point(relative);
        wf::pointf_t output_offset = {0, 0};
        if (view->get_output())
        {
            auto output = view->get_output()->get_layout_geometry();
            output_offset.x = output.x;
            output_offset.y = output.y;
        }

        return {relative.x + output_offset.x, relative.y + output_offset.y};
    }

    inline static double distance_between_points(const wf::pointf_t& a,
        const wf::pointf_t& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    inline static wf::pointf_t region_closest_point(const wf::region_t& region,
        const wf::pointf_t& ref)
    {
        if (region.empty() || region.contains_pointf(ref))
        {
            return ref;
        }

        auto extents = region.get_extents();
        wf::pointf_t result = {1.0 * extents.x1, 1.0 * extents.y1};

        for (const auto& box : region)
        {
            auto wlr_box = wlr_box_from_pixman_box(box);

            double x, y;
            wlr_box_closest_point(&wlr_box, ref.x, ref.y, &x, &y);
            wf::pointf_t closest = {x, y};

            if (distance_between_points(ref, result) >
                distance_between_points(ref, closest))
            {
                result = closest;
            }
        }

        return result;
    }

    wf::pointf_t constrain_point(wf::pointf_t point)
    {
        point = get_surface_relative_coords(this->last_surface, point);
        auto closest = region_closest_point({&this->last_constraint->region}, point);
        closest = get_absolute_position_from_relative(closest);

        return closest;
    }

    // A handler for pointer motion events before they are passed to the scenegraph.
    // Necessary for the implementation of pointer-constraints and relative-pointer.
    wf::signal_connection_t on_pointer_motion = [=] (wf::signal_data_t *data)
    {
        auto evv = static_cast<
            wf::input_event_signal<wlr_event_pointer_motion>*>(data);
        auto ev    = evv->event;
        auto& seat = wf::get_core_impl().seat;

        // First, we send relative pointer motion as in the raw event, so that
        // clients get the correct delta independently of the pointer constraint.
        wlr_relative_pointer_manager_v1_send_relative_motion(
            wf::get_core().protocols.relative_pointer, seat->seat,
            (uint64_t)ev->time_msec * 1000, ev->delta_x, ev->delta_y,
            ev->unaccel_dx, ev->unaccel_dy);

        double dx = ev->delta_x;
        double dy = ev->delta_y;

        if (last_constraint)
        {
            wf::pointf_t gc     = wf::get_core().get_cursor_position();
            wf::pointf_t target = gc;

            switch (last_constraint->type)
            {
              case WLR_POINTER_CONSTRAINT_V1_CONFINED:
                target = constrain_point({gc.x + dx, gc.y + dy});
                break;

              case WLR_POINTER_CONSTRAINT_V1_LOCKED:
                break;
            }

            ev->delta_x = target.x - gc.x;
            ev->delta_y = target.y - gc.y;
        }
    };

    void _check_activate_constraint()
    {
        // No constraints for compositor surfaces
        if (!last_surface || !last_surface->get_wlr_surface())
        {
            _reset_constraint();
            return;
        }

        auto& seat = wf::get_core_impl().seat;
        auto constraint = wlr_pointer_constraints_v1_constraint_for_surface(
            wf::get_core().protocols.pointer_constraints,
            last_surface->get_wlr_surface(), seat->seat);

        if (constraint == last_constraint)
        {
            return;
        }

        _reset_constraint();
        if (!constraint)
        {
            return;
        }

        constraint_destroyed.set_callback([=] (void*)
        {
            last_constraint = NULL;
            constraint_destroyed.disconnect();
        });

        constraint_destroyed.connect(&constraint->events.destroy);
        wlr_pointer_constraint_v1_send_activated(constraint);
        last_constraint = constraint;
    }

    void _reset_constraint()
    {
        if (!this->last_constraint)
        {
            return;
        }

        constraint_destroyed.disconnect();
        wlr_pointer_constraint_v1_send_deactivated(last_constraint);
        last_constraint = NULL;
    }

    void _set_current_surface(wf::surface_interface_t *focus,
        wf::pointf_t local_pos, bool is_dnd)
    {
        assert(focus);
        if (last_surface == focus)
        {
            return;
        }

        auto seat = wf::get_core_impl().get_current_seat();
        if (auto cs = compositor_surface_from_surface(last_surface))
        {
            cs->on_pointer_leave();
        }

        if (auto cs = compositor_surface_from_surface(focus))
        {
            cs->on_pointer_enter(local_pos.x, local_pos.y);
        } else
        {
            wlr_seat_pointer_notify_enter(seat, focus->get_wlr_surface(),
                local_pos.x, local_pos.y);
        }

        last_surface = focus;

        if (!is_dnd)
        {
            _check_activate_constraint();
        }

        if (focus->get_wlr_surface())
        {
            wf::xwayland_bring_to_front(focus->get_wlr_surface());
        }
    }

    void _clear_focus()
    {
        if (!last_surface)
        {
            return;
        }

        auto seat = wf::get_core_impl().get_current_seat();
        if (auto cs = compositor_surface_from_surface(last_surface))
        {
            cs->on_pointer_leave();
        } else if (seat->pointer_state.focused_surface ==
                   last_surface->get_wlr_surface())
        {
            // We defocus only if our surface is still focused on the seat.
            wlr_seat_pointer_notify_clear_focus(seat);
        }

        last_surface = nullptr;
        _reset_constraint();
    }

  public:
    view_pointer_interaction_t(wayfire_view view)
    {
        this->view = view;

        on_surface_map_state_change.set_callback(
            [=] (wf::surface_interface_t *changed)
        {
            if (this->last_surface == changed)
            {
                _clear_focus();
            }
        });
    }

    bool accepts_input(wf::pointf_t point) final
    {
        // FIXME: a better implementation which does not rely on find_node_at.
        // find_node_at should be simplified and this should get the full cases.
        return view->get_main_node()->find_node_at(point).has_value();
    }

    wf::input_action handle_pointer_button(const wlr_event_pointer_button& event)
    final
    {
        nr_held_buttons += (event.state == WLR_BUTTON_PRESSED ? 1 : -1);
        if (auto cs = wf::compositor_surface_from_surface(last_surface))
        {
            cs->on_pointer_button(event.button, event.state);
        }

        auto seat = wf::get_core_impl().get_current_seat();
        LOGI("Button happening", event.button, event.state);
        wlr_seat_pointer_notify_button(seat,
            event.time_msec, event.button, event.state);
        return wf::input_action::CONSUME;
    }

    void _update_focus(wf::pointf_t global, wf::pointf_t& local_coords)
    {
        if (view->get_output())
        {
            auto og = view->get_output()->get_layout_geometry();
            global.x -= og.x;
            global.y -= og.y;
        }

        auto surf = view->map_input_coordinates(global, local_coords);
        _set_current_surface(surf, local_coords, false);
    }

    void handle_pointer_enter(wf::pointf_t position) final
    {
        wf::pointf_t local;
        _update_focus(position, local);
        wf::get_core().connect_signal("pointer_motion", &on_pointer_motion);
    }

    wf::input_action handle_pointer_motion(wf::pointf_t pointer_position,
        uint32_t time_ms) final
    {
        auto& seat = wf::get_core_impl().seat;
        if (seat->drag_active)
        {
            // Special mode: when drag-and-drop is active, we get an implicit
            // grab on the originating node. So, the original node receives all
            // possible events. It then needs to make sure that the correct node
            // receives the event.
            handle_motion_dnd(pointer_position, time_ms);
            return wf::input_action::CONSUME;
        }

        wf::pointf_t local;
        if ((nr_held_buttons == 0) || !this->last_surface)
        {
            _update_focus(pointer_position, local);
        } else if (this->last_surface)
        {
            local = get_surface_relative_coords(
                this->last_surface, pointer_position);
        }

        _send_motion_to_focus(time_ms, local);
        return wf::input_action::CONSUME;
    }

    void _send_motion_to_focus(uint32_t time_ms, wf::pointf_t local)
    {
        auto seat = wf::get_core_impl().get_current_seat();
        if (auto cs = wf::compositor_surface_from_surface(last_surface))
        {
            cs->on_pointer_motion(local.x, local.y);
        } else if (last_surface)
        {
            wlr_seat_pointer_notify_motion(seat, time_ms, local.x, local.y);
        }
    }

    wf::input_action handle_pointer_axis(const wlr_event_pointer_axis& ev) final
    {
        if (auto cs = wf::compositor_surface_from_surface(last_surface))
        {
            cs->on_pointer_axis(ev.orientation, ev.delta, ev.delta_discrete);
        } else
        {
            auto seat = wf::get_core_impl().get_current_seat();
            wlr_seat_pointer_notify_axis(seat, ev.time_msec, ev.orientation,
                ev.delta, ev.delta_discrete, ev.source);
        }

        return wf::input_action::CONSUME;
    }

    void handle_pointer_leave() final
    {
        this->nr_held_buttons = 0;
        _clear_focus();
        on_pointer_motion.disconnect();
    }

    // ---------------------------- DnD implementation ---------------------- */
    void handle_motion_dnd(wf::pointf_t pointer_position, uint32_t time_ms)
    {
        _reset_constraint();
        auto node = wf::get_core().scene()->find_node_at(pointer_position);
        if (node && node->surface && node->surface->get_wlr_surface())
        {
            if (node->surface != this->last_surface)
            {
                _clear_focus();
            }

            auto seat = wf::get_core().get_current_seat();
            wlr_seat_pointer_notify_enter(seat, node->surface->get_wlr_surface(),
                node->local_coords.x, node->local_coords.y);
            wlr_seat_pointer_notify_motion(seat, time_ms,
                node->local_coords.x, node->local_coords.y);

            LOGI("Motion dnd is now over ", pointer_position, " ", node->surface);
        }
    }
};
