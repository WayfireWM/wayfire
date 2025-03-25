#include <wayfire/per-output-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/txn/transaction-manager.hpp>

#include "deco-subsurface.hpp"
#include "wayfire/core.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/toplevel.hpp"

class wayfire_decoration : public wf::plugin_interface_t
{
    wf::view_matcher_t ignore_views{"decoration/ignore_views"};
    wf::view_matcher_t forced_views{"decoration/forced_views"};

    wf::signal::connection_t<wf::txn::new_transaction_signal> on_new_tx =
        [=] (wf::txn::new_transaction_signal *ev)
    {
        // For each transaction, we need to consider what happens with participating views
        for (const auto& obj : ev->tx->get_objects())
        {
            if (auto toplevel = std::dynamic_pointer_cast<wf::toplevel_t>(obj))
            {
                // First check whether the toplevel already has decoration
                // In that case, we should just set the correct margins
                if (auto deco = toplevel->get_data<wf::simple_decorator_t>())
                {
                    toplevel->pending().margins = deco->get_margins(toplevel->pending());
                    continue;
                }

                // Second case: the view is already mapped, or the transaction does not map it.
                // The view is not being decorated, so nothing to do here.
                if (toplevel->current().mapped || !toplevel->pending().mapped)
                {
                    continue;
                }

                // Third case: the transaction will map the toplevel.
                auto view = wf::find_view_for_toplevel(toplevel);
                wf::dassert(view != nullptr, "Mapping a toplevel means there must be a corresponding view!");
                if (should_decorate_view(view))
                {
                    adjust_new_decorations(view);
                }
            }
        }
    };

    wf::signal::connection_t<wf::view_decoration_state_updated_signal> on_decoration_state_changed =
        [=] (wf::view_decoration_state_updated_signal *ev)
    {
        update_view_decoration(ev->view);
    };

    // allows criteria containing maximized or floating check
    wf::signal::connection_t<wf::view_tiled_signal> on_view_tiled =
        [=] (wf::view_tiled_signal *ev)
    {
        update_view_decoration(ev->view);
    };

  public:
    void init() override
    {
        wf::get_core().connect(&on_decoration_state_changed);
        wf::get_core().tx_manager->connect(&on_new_tx);
        wf::get_core().connect(&on_view_tiled);

        for (auto& view : wf::get_core().get_all_views())
        {
            update_view_decoration(view);
        }
    }

    void fini() override
    {
        for (auto view : wf::get_core().get_all_views())
        {
            if (auto toplevel = wf::toplevel_cast(view))
            {
                remove_decoration(toplevel);
                wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
            }
        }
    }

    /**
     * Uses view_matcher_t to match whether the given view needs to be
     * ignored for decoration
     *
     * @param view The view to match
     * @return Whether the given view should be decorated?
     */
    bool ignore_decoration_of_view(wayfire_view view)
    {
        return ignore_views.matches(view);
    }

    /**
     * Uses view_matcher_t to match whether to force decorations onto the
     * given view
     *
     * @param view The view to match
     * @return Whether the given view should be decorated?
     */
    bool force_decoration_of_view(wayfire_view view)
    {
        return forced_views.matches(view);
    }

    bool should_decorate_view(wayfire_toplevel_view view)
    {
        return !ignore_decoration_of_view(view) &&
               (force_decoration_of_view(view) || view->should_be_decorated());
    }

    void adjust_new_decorations(wayfire_toplevel_view view)
    {
        auto toplevel = view->toplevel();

        toplevel->store_data(std::make_unique<wf::simple_decorator_t>(view));
        auto deco     = toplevel->get_data<wf::simple_decorator_t>();
        auto& pending = toplevel->pending();
        pending.margins = deco->get_margins(pending);

        if (!pending.fullscreen && !pending.tiled_edges)
        {
            pending.geometry = wf::expand_geometry_by_margins(pending.geometry, pending.margins);
            if (view->get_output())
            {
                pending.geometry = wf::clamp(pending.geometry, view->get_output()->workarea->get_workarea());
            }
        }
    }

    void remove_decoration(wayfire_toplevel_view view)
    {
        view->toplevel()->erase_data<wf::simple_decorator_t>();
        auto& pending = view->toplevel()->pending();
        if (!pending.fullscreen && !pending.tiled_edges)
        {
            pending.geometry = wf::shrink_geometry_by_margins(pending.geometry, pending.margins);
        }

        pending.margins = {0, 0, 0, 0};
    }

    bool is_toplevel_decorated(const std::shared_ptr<wf::toplevel_t>& toplevel)
    {
        return toplevel->has_data<wf::simple_decorator_t>();
    }

    void update_view_decoration(wayfire_view view)
    {
        if (auto toplevel = wf::toplevel_cast(view))
        {
            const bool wants_decoration = should_decorate_view(toplevel);
            if (wants_decoration != is_toplevel_decorated(toplevel->toplevel()))
            {
                if (wants_decoration)
                {
                    adjust_new_decorations(toplevel);
                } else
                {
                    remove_decoration(toplevel);
                }

                wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
            }
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_decoration);
