#include <wayfire/singleton-plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>

#include "deco-subsurface.hpp"

struct wayfire_decoration_global_cleanup_t
{
    wayfire_decoration_global_cleanup_t() = default;
    ~wayfire_decoration_global_cleanup_t()
    {
        for (auto view : wf::get_core().get_all_views())
        {
            deinit_view(view);
        }
    }

    wayfire_decoration_global_cleanup_t(const wayfire_decoration_global_cleanup_t &)
    = delete;
    wayfire_decoration_global_cleanup_t(wayfire_decoration_global_cleanup_t &&) =
    delete;
    wayfire_decoration_global_cleanup_t& operator =(
        const wayfire_decoration_global_cleanup_t&) = delete;
    wayfire_decoration_global_cleanup_t& operator =(
        wayfire_decoration_global_cleanup_t&&) = delete;
};

class wayfire_decoration :
    public wf::singleton_plugin_t<wayfire_decoration_global_cleanup_t, true>
{
    wf::view_matcher_t ignore_views{"decoration/ignore_views"};

    wf::signal_connection_t view_updated{
        [=] (wf::signal_data_t *data)
        {
            update_view_decoration(get_signaled_view(data));
        }
    };

  public:
    void init() override
    {
        singleton_plugin_t::init();
        grab_interface->name = "simple-decoration";
        grab_interface->capabilities = wf::CAPABILITY_VIEW_DECORATOR;

        output->connect_signal("view-mapped", &view_updated);
        output->connect_signal("view-decoration-state-updated", &view_updated);
        for (auto& view :
             output->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            update_view_decoration(view);
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

    wf::wl_idle_call idle_deactivate;
    void update_view_decoration(wayfire_view view)
    {
        if (view->should_be_decorated() && !ignore_decoration_of_view(view))
        {
            if (output->activate_plugin(grab_interface))
            {
                init_view(view);
                idle_deactivate.run_once([this] ()
                {
                    output->deactivate_plugin(grab_interface);
                });
            }
        } else
        {
            deinit_view(view);
        }
    }

    void fini() override
    {
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            deinit_view(view);
        }

        singleton_plugin_t::fini();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_decoration);
