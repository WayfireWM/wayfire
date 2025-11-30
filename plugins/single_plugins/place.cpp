#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include <wayfire/debug.hpp>
#include "wayfire/txn/transaction-manager.hpp"
#include "wayfire/plugin.hpp"
#include <wayfire/view.hpp>
#include <wayfire/core.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/signal-definitions.hpp>

class wayfire_place_cascade_data : public wf::custom_data_t
{
  public:
    int x = 0;
    int y = 0;
};

class wayfire_place_window : public wf::plugin_interface_t
{
    wf::signal::connection_t<wf::txn::new_transaction_signal> on_new_tx =
        [=] (wf::txn::new_transaction_signal *ev)
    {
        // For each transaction, we need to consider what happens with participating views
        for (const auto& obj : ev->tx->get_objects())
        {
            auto toplevel = std::dynamic_pointer_cast<wf::toplevel_t>(obj);
            if (!toplevel || !map_pending(toplevel))
            {
                continue;
            }

            auto view = wf::find_view_for_toplevel(toplevel);
            if (view && should_place(view))
            {
                do_place(view);
            }
        }
    };

    bool map_pending(std::shared_ptr<wf::toplevel_t> toplevel)
    {
        return !toplevel->current().mapped && toplevel->pending().mapped;
    }

    bool should_place(wayfire_toplevel_view toplevel)
    {
        if (toplevel->parent)
        {
            return false;
        }

        if (toplevel->pending_fullscreen() || toplevel->pending_tiled_edges())
        {
            return false;
        }

        if (toplevel->has_property("startup-x") || toplevel->has_property("startup-y"))
        {
            return false;
        }

        if (!toplevel->get_output())
        {
            return false;
        }

        return true;
    }

    void do_place(wayfire_toplevel_view view)
    {
        auto output   = view->get_output();
        auto workarea = output->workarea->get_workarea();

        std::string mode = placement_mode;
        if (mode == "cascade")
        {
            cascade(view, workarea);
        } else if (mode == "maximize")
        {
            maximize(view, workarea);
        } else if (mode == "random")
        {
            random(view, workarea);
        } else if (mode == "pointer")
        {
            pointer(view, workarea);
        } else
        {
            center(view, workarea);
        }
    }

    void adjust_cascade_for_workarea(nonstd::observer_ptr<wayfire_place_cascade_data> cascade,
        wf::geometry_t workarea)
    {
        if ((cascade->x < workarea.x) || (cascade->x > workarea.x + workarea.width))
        {
            cascade->x = workarea.x;
        }

        if ((cascade->y < workarea.y) || (cascade->y > workarea.y + workarea.height))
        {
            cascade->y = workarea.y;
        }
    }

    wf::option_wrapper_t<std::string> placement_mode{"place/mode"};

  public:
    void init() override
    {
        wf::get_core().tx_manager->connect(&on_new_tx);
    }

    void cascade(wayfire_toplevel_view view, wf::geometry_t workarea)
    {
        wf::geometry_t window = view->get_pending_geometry();
        auto cascade = view->get_output()->get_data_safe<wayfire_place_cascade_data>();
        adjust_cascade_for_workarea(cascade, workarea);

        if ((cascade->x + window.width > workarea.x + workarea.width) ||
            (cascade->y + window.height > workarea.y + workarea.height))
        {
            cascade->x = workarea.x;
            cascade->y = workarea.y;
        }

        view->toplevel()->pending().geometry.x = cascade->x;
        view->toplevel()->pending().geometry.y = cascade->y;

        cascade->x += workarea.width * .03;
        cascade->y += workarea.height * .03;
    }

    void random(wayfire_toplevel_view & view, wf::geometry_t workarea)
    {
        wf::geometry_t window = view->get_pending_geometry();
        wf::geometry_t area;

        area.x     = workarea.x;
        area.y     = workarea.y;
        area.width = workarea.width - window.width;
        area.height = workarea.height - window.height;

        if ((area.width <= 0) || (area.height <= 0))
        {
            center(view, workarea);

            return;
        }

        view->toplevel()->pending().geometry.x = rand() % area.width + area.x;
        view->toplevel()->pending().geometry.y = rand() % area.height + area.y;
    }

    void center(wayfire_toplevel_view & view, wf::geometry_t workarea)
    {
        wf::geometry_t window = view->get_pending_geometry();
        view->toplevel()->pending().geometry.x = workarea.x + (workarea.width / 2) - (window.width / 2);
        view->toplevel()->pending().geometry.y = workarea.y + (workarea.height / 2) - (window.height / 2);
    }

    void pointer(wayfire_toplevel_view & view, wf::geometry_t workarea)
    {
        wf::output_t *output = view->get_output();
        if (!output)
        {
            return;
        }

        wf::point_t pos = output->get_cursor_position().round_down();
        wf::geometry_t window = view->get_pending_geometry();
        window.x = workarea.x + std::clamp(pos.x - window.width / 2,
            0, workarea.width - window.width);
        window.y = workarea.y + std::clamp(pos.y - window.height / 2,
            0, workarea.height - window.height);
        view->toplevel()->pending().geometry.x = window.x;
        view->toplevel()->pending().geometry.y = window.y;
    }

    void maximize(wayfire_toplevel_view & view, wf::geometry_t workarea)
    {
        wf::get_core().default_wm->tile_request(view, wf::TILED_EDGES_ALL);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_place_window);
