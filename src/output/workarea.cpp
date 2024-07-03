#include <wayfire/workarea.hpp>
#include "wayfire/geometry.hpp"
#include "wayfire/signal-provider.hpp"
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output-layout.hpp>

struct wf::output_workarea_manager_t::impl
{
    wf::geometry_t current_workarea;
    std::vector<anchored_area*> anchors;
    output_t *output;
    wf::signal::connection_t<output_configuration_changed_signal> on_configuration_changed;
};

wf::output_workarea_manager_t::output_workarea_manager_t(output_t *output)
{
    priv = std::make_unique<impl>();
    priv->output = output;
    priv->current_workarea = output->get_relative_geometry();
    priv->on_configuration_changed = [=] (auto)
    {
        this->reflow_reserved_areas();
    };
    output->connect(&priv->on_configuration_changed);
}

wf::output_workarea_manager_t::~output_workarea_manager_t() = default;

wf::geometry_t wf::output_workarea_manager_t::get_workarea()
{
    return priv->current_workarea;
}

void wf::output_workarea_manager_t::add_reserved_area(anchored_area *area)
{
    priv->anchors.push_back(area);
}

void wf::output_workarea_manager_t::remove_reserved_area(anchored_area *area)
{
    auto it = std::remove(priv->anchors.begin(), priv->anchors.end(), area);
    priv->anchors.erase(it, priv->anchors.end());
}

void wf::output_workarea_manager_t::reflow_reserved_areas()
{
    auto old_workarea = priv->current_workarea;

    priv->current_workarea = priv->output->get_relative_geometry();
    for (auto a : priv->anchors)
    {
        if (a->reflowed)
        {
            a->reflowed(priv->current_workarea);
        }

        switch (a->edge)
        {
          case ANCHORED_EDGE_TOP:
            priv->current_workarea.y += a->reserved_size;

          // fallthrough
          case ANCHORED_EDGE_BOTTOM:
            priv->current_workarea.height -= a->reserved_size;
            break;

          case ANCHORED_EDGE_LEFT:
            priv->current_workarea.x += a->reserved_size;

          // fallthrough
          case ANCHORED_EDGE_RIGHT:
            priv->current_workarea.width -= a->reserved_size;
            break;
        }
    }

    wf::workarea_changed_signal data;
    data.output = priv->output;
    data.old_workarea = old_workarea;
    data.new_workarea = priv->current_workarea;

    if (data.old_workarea != data.new_workarea)
    {
        priv->output->emit(&data);
    }
}
