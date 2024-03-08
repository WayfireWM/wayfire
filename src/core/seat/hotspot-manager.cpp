#include "hotspot-manager.hpp"
#include "wayfire/core.hpp"
#include <wayfire/output-layout.hpp>
#include <wayfire/touch/touch.hpp>

void wf::hotspot_instance_t::process_input_motion(wf::pointf_t gc)
{
    const auto& reset_hotspot = [&] ()
    {
        timer.disconnect();
        this->armed = true;
    };

    auto target = wf::get_core().output_layout->get_output_coords_at(gc, gc);
    if (target != last_output)
    {
        reset_hotspot();
        last_output = target;
        recalc_geometry();
    }

    if (!(hotspot_geometry[0] & gc) && !(hotspot_geometry[1] & gc))
    {
        reset_hotspot();
        return;
    }

    if (!timer.is_connected() && this->armed)
    {
        this->armed = false;
        timer.set_timeout(timeout_ms, [=] ()
        {
            callback(this->edges);
            return false;
        });
    }
}

wf::geometry_t wf::hotspot_instance_t::pin(wf::dimensions_t dim) noexcept
{
    if (!last_output)
    {
        return {0, 0, 0, 0};
    }

    auto og = last_output->get_layout_geometry();

    wf::geometry_t result;
    result.width  = dim.width;
    result.height = dim.height;

    if (this->edges & OUTPUT_EDGE_LEFT)
    {
        result.x = og.x;
    } else if (this->edges & OUTPUT_EDGE_RIGHT)
    {
        result.x = og.x + og.width - dim.width;
    } else
    {
        result.x = og.x + og.width / 2 - dim.width / 2;
    }

    if (this->edges & OUTPUT_EDGE_TOP)
    {
        result.y = og.y;
    } else if (this->edges & OUTPUT_EDGE_BOTTOM)
    {
        result.y = og.y + og.height - dim.height;
    } else
    {
        result.y = og.y + og.height / 2 - dim.height / 2;
    }

    // Need to clamp if the region is very wide
    return wf::clamp(result, og);
}

void wf::hotspot_instance_t::recalc_geometry() noexcept
{
    uint32_t cnt_edges = __builtin_popcount(edges);

    if (cnt_edges == 2)
    {
        hotspot_geometry[0] = pin({away, along});
        hotspot_geometry[1] = pin({along, away});
    } else
    {
        wf::dimensions_t dim;
        if (edges & (OUTPUT_EDGE_LEFT | OUTPUT_EDGE_RIGHT))
        {
            dim = {away, along};
        } else
        {
            dim = {along, away};
        }

        hotspot_geometry[0] = pin(dim);
        hotspot_geometry[1] = hotspot_geometry[0];
    }
}

wf::hotspot_instance_t::hotspot_instance_t(uint32_t edges, uint32_t along, uint32_t away, int32_t timeout,
    std::function<void(uint32_t)> callback)
{
    wf::get_core().connect(&on_motion_event);
    wf::get_core().connect(&on_motion_event);
    wf::get_core().connect(&on_touch_motion);

    this->edges = edges;
    this->along = along;
    this->away  = away;
    this->timeout_ms = timeout;
    this->callback   = callback;

    recalc_geometry();

    // callbacks
    on_tablet_axis = [=] (wf::post_input_event_signal<wlr_tablet_tool_axis_event> *ev)
    {
        process_input_motion(wf::get_core().get_cursor_position());
    };

    on_motion_event = [=] (auto)
    {
        process_input_motion(wf::get_core().get_cursor_position());
    };

    on_touch_motion = [=] (wf::post_input_event_signal<wlr_touch_motion_event> *ev)
    {
        const auto& state = wf::get_core().get_touch_state();
        if (state.fingers.count(0) && (ev->event->touch_id == 0))
        {
            process_input_motion(wf::get_core().get_touch_position(0));
        }
    };
}

void wf::hotspot_manager_t::update_hotspots(const container_t& activators)
{
    hotspots.clear();
    for (const auto& opt : activators)
    {
        auto opt_hotspots = opt->activated_by->get_value().get_hotspots();
        for (const auto& hs : opt_hotspots)
        {
            auto activator_cb = opt->callback;
            auto callback = [activator_cb] (uint32_t edges)
            {
                wf::activator_data_t data = {
                    .source = activator_source_t::HOTSPOT,
                    .activation_data = edges,
                };
                (*activator_cb)(data);
            };

            auto instance = std::make_unique<hotspot_instance_t>(hs.get_edges(),
                hs.get_size_along_edge(), hs.get_size_away_from_edge(), hs.get_timeout(), callback);
            hotspots.push_back(std::move(instance));
        }
    }
}
