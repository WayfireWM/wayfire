#include <wayfire/per-output-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>

#include "deco-subsurface.hpp"
#include "wayfire/core.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/signal-provider.hpp"

class wayfire_decoration : public wf::plugin_interface_t
{
    wf::view_matcher_t ignore_views{"decoration/ignore_views"};

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        update_view_decoration(ev->view);
    };

    wf::signal::connection_t<wf::view_decoration_state_updated_signal> on_decoration_state_changed =
        [=] (wf::view_decoration_state_updated_signal *ev)
    {
        update_view_decoration(ev->view);
    };

  public:
    void init() override
    {
        wf::get_core().connect(&on_decoration_state_changed);
        wf::get_core().connect(&on_view_mapped);

        for (auto& view : wf::get_core().get_all_views())
        {
            update_view_decoration(view);
        }
    }

    void fini() override
    {
        for (auto view : wf::get_core().get_all_views())
        {
            deinit_view(view);
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

    void update_view_decoration(wayfire_view view)
    {
        if (view->should_be_decorated() && !ignore_decoration_of_view(view))
        {
            init_view(view);
        } else
        {
            deinit_view(view);
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_decoration);
