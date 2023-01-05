#include <wayfire/per-output-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>

#include "deco-subsurface.hpp"
#include "wayfire/plugin.hpp"

class wayfire_decoration : public wf::plugin_interface_t, private wf::per_output_tracker_mixin_t<>
{
    wf::view_matcher_t ignore_views{"decoration/ignore_views"};
    wf::signal_connection_t view_updated = [=] (wf::signal_data_t *data)
    {
        update_view_decoration(get_signaled_view(data));
    };

  public:
    void init() override
    {
        this->init_output_tracking();
    }

    void fini() override
    {
        for (auto view : wf::get_core().get_all_views())
        {
            deinit_view(view);
        }
    }

    wf::plugin_activation_data_t grab_interface{
        .name = "simple-decoration",
        .capabilities = wf::CAPABILITY_VIEW_DECORATOR,
    };

    void handle_new_output(wf::output_t *output) override
    {
        output->connect_signal("view-mapped", &view_updated);
        output->connect_signal("view-decoration-state-updated", &view_updated);
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            update_view_decoration(view);
        }
    }

    void handle_output_removed(wf::output_t *output) override
    {
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
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
