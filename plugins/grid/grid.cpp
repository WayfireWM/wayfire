#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/render-manager.hpp>
#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>
#include "wayfire/plugin.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/plugins/common/geometry-animation.hpp>
#include "wayfire/plugins/grid.hpp"
#include "wayfire/plugins/crossfade.hpp"
#include <wayfire/window-manager.hpp>

#include <wayfire/plugins/wobbly/wobbly-signal.hpp>
#include <wayfire/view-transform.hpp>

const std::string grid_view_id = "grid-view";

class wf_grid_slot_data : public wf::custom_data_t
{
  public:
    int slot;
};

nonstd::observer_ptr<wf::grid::grid_animation_t> ensure_grid_view(wayfire_toplevel_view view)
{
    if (!view->has_data<wf::grid::grid_animation_t>())
    {
        wf::option_wrapper_t<std::string> animation_type{"grid/type"};
        wf::option_wrapper_t<int> duration{"grid/duration"};

        wf::grid::grid_animation_t::type_t type = wf::grid::grid_animation_t::NONE;
        if (animation_type.value() == "crossfade")
        {
            type = wf::grid::grid_animation_t::CROSSFADE;
        } else if (animation_type.value() == "wobbly")
        {
            type = wf::grid::grid_animation_t::WOBBLY;
        }

        view->store_data(
            std::make_unique<wf::grid::grid_animation_t>(view, type, duration));
    }

    return view->get_data<wf::grid::grid_animation_t>();
}

/*
 * 7 8 9
 * 4 5 6
 * 1 2 3
 */
static uint32_t get_tiled_edges_for_slot(uint32_t slot)
{
    if (slot == 0)
    {
        return 0;
    }

    uint32_t edges = wf::TILED_EDGES_ALL;
    if (slot % 3 == 0)
    {
        edges &= ~WLR_EDGE_LEFT;
    }

    if (slot % 3 == 1)
    {
        edges &= ~WLR_EDGE_RIGHT;
    }

    if (slot <= 3)
    {
        edges &= ~WLR_EDGE_TOP;
    }

    if (slot >= 7)
    {
        edges &= ~WLR_EDGE_BOTTOM;
    }

    return edges;
}

static uint32_t get_slot_from_tiled_edges(uint32_t edges)
{
    for (int slot = 0; slot <= 9; slot++)
    {
        if (get_tiled_edges_for_slot(slot) == edges)
        {
            return slot;
        }
    }

    return 0;
}

class wayfire_grid : public wf::per_output_plugin_instance_t
{
    std::vector<std::string> slots =
    {"unused", "bl", "b", "br", "l", "c", "r", "tl", "t", "tr"};
    wf::activator_callback bindings[10];
    wf::option_wrapper_t<wf::activatorbinding_t> keys[10];
    wf::option_wrapper_t<wf::activatorbinding_t> restore_opt{"grid/restore"};

    wf::plugin_activation_data_t grab_interface{
        .name = "grid",
        .capabilities = wf::CAPABILITY_MANAGE_DESKTOP,
    };

    wf::activator_callback restore = [=] (auto)
    {
        if (!output->can_activate_plugin(&grab_interface))
        {
            return false;
        }

        auto view = toplevel_cast(output->get_active_view());
        if (!view)
        {
            return false;
        }

        wf::get_core().default_wm->tile_request(view, 0);
        return true;
    };

  public:
    void init() override
    {
        for (int i = 1; i < 10; i++)
        {
            keys[i].load_option("grid/slot_" + slots[i]);
            bindings[i] = [=] (auto)
            {
                auto view = toplevel_cast(output->get_active_view());
                if (!view || (view->role != wf::VIEW_ROLE_TOPLEVEL))
                {
                    return false;
                }

                if (!output->can_activate_plugin(wf::CAPABILITY_MANAGE_DESKTOP))
                {
                    return false;
                }

                handle_slot(view, i);
                return true;
            };

            output->add_activator(keys[i], &bindings[i]);
        }

        output->add_activator(restore_opt, &restore);

        output->connect(&on_workarea_changed);
        output->connect(&on_snap_signal);
        output->connect(&on_snap_query);
        output->connect(&on_maximize_signal);
        output->connect(&on_fullscreen_signal);
    }

    bool can_adjust_view(wayfire_toplevel_view view)
    {
        uint32_t req_actions = wf::VIEW_ALLOW_MOVE | wf::VIEW_ALLOW_RESIZE;
        return (view->get_allowed_actions() & req_actions) == req_actions;
    }

    void handle_slot(wayfire_toplevel_view view, int slot, wf::point_t delta = {0, 0})
    {
        if (!can_adjust_view(view))
        {
            return;
        }

        view->get_data_safe<wf_grid_slot_data>()->slot = slot;
        ensure_grid_view(view)->adjust_target_geometry(
            get_slot_dimensions(slot) + delta,
            get_tiled_edges_for_slot(slot));
    }

    /*
     * 7 8 9
     * 4 5 6
     * 1 2 3
     * */
    wf::geometry_t get_slot_dimensions(int n)
    {
        auto area = output->workarea->get_workarea();
        int w2    = area.width / 2;
        int h2    = area.height / 2;

        if (n % 3 == 1)
        {
            area.width = w2;
        }

        if (n % 3 == 0)
        {
            area.width = w2, area.x += w2;
        }

        if (n >= 7)
        {
            area.height = h2;
        } else if (n <= 3)
        {
            area.height = h2, area.y += h2;
        }

        return area;
    }

    wf::signal::connection_t<wf::workarea_changed_signal> on_workarea_changed =
        [=] (wf::workarea_changed_signal *ev)
    {
        for (auto& view : output->wset()->get_views(wf::WSET_MAPPED_ONLY))
        {
            auto data = view->get_data_safe<wf_grid_slot_data>();

            /* Detect if the view was maximized outside of the grid plugin */
            auto wm = view->get_wm_geometry();
            if (view->tiled_edges && (wm.width == ev->old_workarea.width) &&
                (wm.height == ev->old_workarea.height))
            {
                data->slot = wf::grid::SLOT_CENTER;
            }

            if (!data->slot)
            {
                continue;
            }

            /* Workarea changed, and we have a view which is tiled into some slot.
             * We need to make sure it remains in its slot. So we calculate the
             * viewport of the view, and tile it there */
            auto output_geometry = output->get_relative_geometry();

            int vx = std::floor(1.0 * wm.x / output_geometry.width);
            int vy = std::floor(1.0 * wm.y / output_geometry.height);

            handle_slot(view, data->slot,
                {vx *output_geometry.width, vy * output_geometry.height});
        }
    };

    wf::signal::connection_t<wf::grid::grid_query_geometry_signal> on_snap_query =
        [=] (wf::grid::grid_query_geometry_signal *query)
    {
        query->out_geometry = get_slot_dimensions(query->slot);
    };

    wf::signal::connection_t<wf::grid::grid_snap_view_signal> on_snap_signal =
        [=] (wf::grid::grid_snap_view_signal *data)
    {
        handle_slot(data->view, data->slot);
    };

    wf::geometry_t adjust_for_workspace(wf::geometry_t geometry,
        wf::point_t workspace)
    {
        auto delta_ws = workspace - output->wset()->get_current_workspace();
        auto scr_size = output->get_screen_size();
        geometry.x += delta_ws.x * scr_size.width;
        geometry.y += delta_ws.y * scr_size.height;
        return geometry;
    }

    wf::signal::connection_t<wf::view_tile_request_signal> on_maximize_signal =
        [=] (wf::view_tile_request_signal *data)
    {
        if (data->carried_out || (data->desired_size.width <= 0) ||
            !can_adjust_view(data->view))
        {
            return;
        }

        data->carried_out = true;
        uint32_t slot = get_slot_from_tiled_edges(data->edges);
        if (slot > 0)
        {
            data->desired_size = get_slot_dimensions(slot);
        }

        data->view->get_data_safe<wf_grid_slot_data>()->slot = slot;
        ensure_grid_view(data->view)->adjust_target_geometry(
            adjust_for_workspace(data->desired_size, data->workspace),
            get_tiled_edges_for_slot(slot));
    };

    wf::signal::connection_t<wf::view_fullscreen_request_signal> on_fullscreen_signal =
        [=] (wf::view_fullscreen_request_signal *data)
    {
        static const std::string fs_data_name = "grid-saved-fs";
        if (data->carried_out || (data->desired_size.width <= 0) ||
            !can_adjust_view(data->view))
        {
            return;
        }

        data->carried_out = true;
        ensure_grid_view(data->view)->adjust_target_geometry(
            adjust_for_workspace(data->desired_size, data->workspace), -1);
    };

    void fini() override
    {
        for (int i = 1; i < 10; i++)
        {
            output->rem_binding(&bindings[i]);
        }

        output->rem_binding(&restore);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_grid>);
