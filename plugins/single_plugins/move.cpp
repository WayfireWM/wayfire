#include "wayfire/plugins/common/input-grab.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/view-helpers.hpp"
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/touch/touch.hpp>
#include <wayfire/plugins/vswitch.hpp>
#include <wayfire/workarea.hpp>

#include <cmath>
#include <linux/input.h>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/preview-indication.hpp>

#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/common/move-drag-interface.hpp>
#include <wayfire/plugins/grid.hpp>

class wayfire_move : public wf::per_output_plugin_instance_t,
    public wf::pointer_interaction_t, public wf::touch_interaction_t
{
    wf::button_callback activate_binding;

    wf::option_wrapper_t<bool> enable_snap{"move/enable_snap"};
    wf::option_wrapper_t<bool> join_views{"move/join_views"};
    wf::option_wrapper_t<int> snap_threshold{"move/snap_threshold"};
    wf::option_wrapper_t<int> quarter_snap_threshold{"move/quarter_snap_threshold"};
    wf::option_wrapper_t<int> workspace_switch_after{"move/workspace_switch_after"};
    wf::option_wrapper_t<wf::buttonbinding_t> activate_button{"move/activate"};

    wf::option_wrapper_t<bool> move_enable_snap_off{"move/enable_snap_off"};
    wf::option_wrapper_t<int> move_snap_off_threshold{"move/snap_off_threshold"};

    bool is_using_touch;
    bool was_client_request;

    struct
    {
        nonstd::observer_ptr<wf::preview_indication_view_t> preview;
        wf::grid::slot_t slot_id = wf::grid::SLOT_NONE;
    } slot;


    wf::wl_timer workspace_switch_timer;

    wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> drag_helper;

    bool can_handle_drag()
    {
        bool yes = output->can_activate_plugin(&grab_interface,
            wf::PLUGIN_ACTIVATE_ALLOW_MULTIPLE);
        return yes;
    }

    wf::signal::connection_t<wf::move_drag::drag_focus_output_signal> on_drag_output_focus =
        [=] (wf::move_drag::drag_focus_output_signal *ev)
    {
        if ((ev->focus_output == output) && can_handle_drag())
        {
            drag_helper->set_scale(1.0);

            if (!output->is_plugin_active(grab_interface.name))
            {
                grab_input(nullptr);
            }
        } else
        {
            update_slot(wf::grid::SLOT_NONE);
        }
    };

    wf::signal::connection_t<wf::move_drag::snap_off_signal> on_drag_snap_off =
        [=] (wf::move_drag::snap_off_signal *ev)
    {
        if ((ev->focus_output == output) && can_handle_drag())
        {
            wf::move_drag::adjust_view_on_snap_off(drag_helper->view);
        }
    };

    wf::signal::connection_t<wf::move_drag::drag_done_signal> on_drag_done =
        [=] (wf::move_drag::drag_done_signal *ev)
    {
        if ((ev->focused_output == output) && can_handle_drag())
        {
            wf::move_drag::adjust_view_on_output(ev);

            if (enable_snap && (slot.slot_id != wf::grid::SLOT_NONE))
            {
                wf::grid::grid_snap_view_signal data;
                data.view = ev->main_view;
                data.slot = slot.slot_id;
                output->emit(&data);

                /* Update slot, will hide the preview as well */
                update_slot(wf::grid::SLOT_NONE);
            }

            wf::view_change_workspace_signal data;
            data.view = ev->main_view;
            data.to   = output->wset()->get_current_workspace();
            data.old_workspace_valid = false;
            output->emit(&data);
        }

        deactivate();
    };

    std::unique_ptr<wf::input_grab_t> input_grab;
    wf::plugin_activation_data_t grab_interface = {
        .name = "move",
        .capabilities = wf::CAPABILITY_GRAB_INPUT | wf::CAPABILITY_MANAGE_DESKTOP,
    };

  public:
    void init() override
    {
        input_grab = std::make_unique<wf::input_grab_t>("move", output, nullptr, this, this);
        activate_binding = [=] (auto)
        {
            is_using_touch     = false;
            was_client_request = false;
            auto view = wf::get_core().get_cursor_focus_view();

            if (view && (view->role != wf::VIEW_ROLE_DESKTOP_ENVIRONMENT))
            {
                initiate(view);
            }

            return false;
        };

        output->add_button(activate_button, &activate_binding);

        using namespace std::placeholders;

        grab_interface.cancel = [=] ()
        {
            input_pressed(WLR_BUTTON_RELEASED);
        };

        output->connect(&move_request);

        drag_helper->connect(&on_drag_output_focus);
        drag_helper->connect(&on_drag_snap_off);
        drag_helper->connect(&on_drag_done);
    }

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        if (event.state != WLR_BUTTON_RELEASED)
        {
            return;
        }

        uint32_t target_button =
            was_client_request ? BTN_LEFT : (wf::buttonbinding_t(activate_button)).get_button();
        if (target_button != event.button)
        {
            return;
        }

        drag_helper->handle_input_released();
        return;
    }

    void handle_pointer_motion(wf::pointf_t pointer_position, uint32_t time_ms) override
    {
        handle_input_motion();
    }

    void handle_touch_up(uint32_t time_ms, int finger_id, wf::pointf_t lift_off_position) override
    {
        if (wf::get_core().get_touch_state().fingers.empty())
        {
            input_pressed(WLR_BUTTON_RELEASED);
        }
    }

    void handle_touch_motion(uint32_t time_ms, int finger_id, wf::pointf_t position) override
    {
        handle_input_motion();
    }

    wf::signal::connection_t<wf::view_move_request_signal> move_request =
        [=] (wf::view_move_request_signal *ev)
    {
        was_client_request = true;
        initiate(ev->view);
    };

    /**
     * Calculate plugin activation flags for the view.
     *
     * Activation flags ignore input inhibitors if the view is in the desktop
     * widget layer (i.e OSKs)
     */
    uint32_t get_act_flags(wayfire_view view)
    {
        auto view_layer = wf::get_view_layer(view).value_or(wf::scene::layer::WORKSPACE);
        /* Allow moving an on-screen keyboard while screen is locked */
        bool ignore_inhibit = view_layer == wf::scene::layer::DWIDGET;
        uint32_t act_flags  = 0;
        if (ignore_inhibit)
        {
            act_flags |= wf::PLUGIN_ACTIVATION_IGNORE_INHIBIT;
        }

        return act_flags;
    }

    /**
     * Calculate the view which is the actual target of this move operation.
     *
     * Usually, this is the view itself or its topmost parent if the join_views
     * option is set.
     */
    wayfire_view get_target_view(wayfire_view view)
    {
        while (view && view->parent && join_views)
        {
            view = view->parent;
        }

        return view;
    }

    bool can_move_view(wayfire_view view)
    {
        if (!view || !view->is_mapped())
        {
            return false;
        }

        view = get_target_view(view);

        auto current_ws_impl =
            output->wset()->get_workspace_implementation();
        if (!current_ws_impl->view_movable(view))
        {
            return false;
        }

        return output->can_activate_plugin(&grab_interface, get_act_flags(view));
    }

    bool grab_input(wayfire_view view)
    {
        view = view ?: drag_helper->view;
        if (!view)
        {
            return false;
        }

        if (!output->activate_plugin(&grab_interface, get_act_flags(view)))
        {
            return false;
        }

        this->input_grab->grab_input(wf::scene::layer::OVERLAY, true);

        auto touch = wf::get_core().get_touch_state();
        is_using_touch = !touch.fingers.empty();
        slot.slot_id   = wf::grid::SLOT_NONE;
        return true;
    }

    bool initiate(wayfire_view view)
    {
        // First, make sure that the view is on the output the input is.
        auto pos = get_global_input_coords();
        auto target_output =
            wf::get_core().output_layout->get_output_at(pos.x, pos.y);

        if (target_output && (view->get_output() != target_output))
        {
            auto offset = wf::origin(view->get_output()->get_layout_geometry()) +
                -wf::origin(target_output->get_layout_geometry());

            wf::get_core().move_view_to_output(view, target_output, false);
            view->move(view->get_wm_geometry().x + offset.x,
                view->get_wm_geometry().y + offset.y);

            // On the new output
            view->move_request();
            return false;
        }

        wayfire_view grabbed_view = view;
        view = get_target_view(view);
        if (!can_move_view(view))
        {
            return false;
        }

        if (!grab_input(view))
        {
            return false;
        }

        wf::move_drag::drag_options_t opts;
        opts.enable_snap_off = move_enable_snap_off &&
            (view->fullscreen || view->tiled_edges);
        opts.snap_off_threshold = move_snap_off_threshold;
        opts.join_views = join_views;

        if (join_views)
        {
            // ensure that the originally grabbed view will be focused
            output->focus_view(grabbed_view);
        }

        drag_helper->start_drag(view, get_global_input_coords(), opts);
        slot.slot_id = wf::grid::SLOT_NONE;
        return true;
    }

    void deactivate()
    {
        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);
    }

    void input_pressed(uint32_t state)
    {
        if (state != WLR_BUTTON_RELEASED)
        {
            return;
        }

        drag_helper->handle_input_released();
    }

    /* Calculate the slot to which the view would be snapped if the input
     * is released at output-local coordinates (x, y) */
    wf::grid::slot_t calc_slot(wf::point_t point)
    {
        auto g = output->workarea->get_workarea();
        if (!(output->get_relative_geometry() & point))
        {
            return wf::grid::SLOT_NONE;
        }

        int threshold = snap_threshold;

        bool is_left   = point.x - g.x <= threshold;
        bool is_right  = g.x + g.width - point.x <= threshold;
        bool is_top    = point.y - g.y < threshold;
        bool is_bottom = g.x + g.height - point.y < threshold;

        bool is_far_left   = point.x - g.x <= quarter_snap_threshold;
        bool is_far_right  = g.x + g.width - point.x <= quarter_snap_threshold;
        bool is_far_top    = point.y - g.y < quarter_snap_threshold;
        bool is_far_bottom = g.x + g.height - point.y < quarter_snap_threshold;

        wf::grid::slot_t slot = wf::grid::SLOT_NONE;
        if ((is_left && is_far_top) || (is_far_left && is_top))
        {
            slot = wf::grid::SLOT_TL;
        } else if ((is_right && is_far_top) || (is_far_right && is_top))
        {
            slot = wf::grid::SLOT_TR;
        } else if ((is_right && is_far_bottom) || (is_far_right && is_bottom))
        {
            slot = wf::grid::SLOT_BR;
        } else if ((is_left && is_far_bottom) || (is_far_left && is_bottom))
        {
            slot = wf::grid::SLOT_BL;
        } else if (is_right)
        {
            slot = wf::grid::SLOT_RIGHT;
        } else if (is_left)
        {
            slot = wf::grid::SLOT_LEFT;
        } else if (is_top)
        {
            // Maximize when dragging to the top
            slot = wf::grid::SLOT_CENTER;
        } else if (is_bottom)
        {
            slot = wf::grid::SLOT_BOTTOM;
        }

        return slot;
    }

    void update_workspace_switch_timeout(wf::grid::slot_t slot_id)
    {
        if ((workspace_switch_after == -1) || (slot_id == wf::grid::SLOT_NONE))
        {
            workspace_switch_timer.disconnect();

            return;
        }

        int dx = 0, dy = 0;
        if (slot_id >= 7)
        {
            dy = -1;
        }

        if (slot_id <= 3)
        {
            dy = 1;
        }

        if (slot_id % 3 == 1)
        {
            dx = -1;
        }

        if (slot_id % 3 == 0)
        {
            dx = 1;
        }

        if ((dx == 0) && (dy == 0))
        {
            workspace_switch_timer.disconnect();

            return;
        }

        wf::point_t cws = output->wset()->get_current_workspace();
        wf::point_t tws = {cws.x + dx, cws.y + dy};
        wf::dimensions_t ws_dim = output->wset()->get_workspace_grid_size();
        wf::geometry_t possible = {
            0, 0, ws_dim.width, ws_dim.height
        };

        /* Outside of workspace grid */
        if (!(possible & tws))
        {
            workspace_switch_timer.disconnect();

            return;
        }

        workspace_switch_timer.set_timeout(workspace_switch_after, [this, tws] ()
        {
            output->wset()->request_workspace(tws);
            return false;
        });
    }

    void update_slot(wf::grid::slot_t new_slot_id)
    {
        /* No changes in the slot, just return */
        if (slot.slot_id == new_slot_id)
        {
            return;
        }

        /* Destroy previous preview */
        if (slot.preview)
        {
            auto input = get_input_coords();
            slot.preview->set_target_geometry({input.x, input.y, 1, 1}, 0, true);
            slot.preview = nullptr;
        }

        slot.slot_id = new_slot_id;

        /* Show a preview overlay */
        if (new_slot_id)
        {
            wf::grid::grid_query_geometry_signal query;
            query.slot = new_slot_id;
            query.out_geometry = {0, 0, -1, -1};
            output->emit(&query);

            /* Unknown slot geometry, can't show a preview */
            if ((query.out_geometry.width <= 0) || (query.out_geometry.height <= 0))
            {
                return;
            }

            auto input   = get_input_coords();
            auto preview =
                new wf::preview_indication_view_t({input.x, input.y, 1, 1});
            wf::get_core().add_view(
                std::unique_ptr<wf::view_interface_t>(preview));
            preview->set_output(output);

            preview->set_target_geometry(query.out_geometry, 1);
            slot.preview = nonstd::make_observer(preview);
        }

        update_workspace_switch_timeout(new_slot_id);
    }

    /* Returns the currently used input coordinates in global compositor space */
    wf::point_t get_global_input_coords()
    {
        wf::pointf_t input;
        if (is_using_touch)
        {
            auto center = wf::get_core().get_touch_state().get_center().current;
            input = {center.x, center.y};
        } else
        {
            input = wf::get_core().get_cursor_position();
        }

        return {(int)input.x, (int)input.y};
    }

    /* Returns the currently used input coordinates in output-local space */
    wf::point_t get_input_coords()
    {
        auto og     = output->get_layout_geometry();
        auto coords = get_global_input_coords() - wf::point_t{og.x, og.y};
        return coords;
    }

    bool is_snap_enabled()
    {
        if (!enable_snap || !drag_helper->view || drag_helper->is_view_held_in_place())
        {
            return false;
        }

        // Make sure that fullscreen views are not tiled.
        // We allow movement of fullscreen views but they should always
        // retain their fullscreen state (but they can be moved to other
        // workspaces). Unsetting the fullscreen state can break some
        // Xwayland games.
        if (drag_helper->view->fullscreen)
        {
            return false;
        }

        if (drag_helper->view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
        {
            return false;
        }

        return true;
    }

    void handle_input_motion()
    {
        drag_helper->handle_motion(get_global_input_coords());
        if (is_snap_enabled())
        {
            update_slot(calc_slot(get_input_coords()));
        }
    }

    void fini() override
    {
        if (input_grab->is_grabbed())
        {
            input_pressed(WLR_BUTTON_RELEASED);
        }

        output->rem_binding(&activate_binding);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_move>);
