#include <wayfire/view.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/util/log.hpp>
#include "plugins/common/wayfire/plugins/common/shared-core-data.hpp"
#include "plugins/ipc/ipc-method-repository.hpp"
#include "plugins/ipc/ipc-helpers.hpp"
#include "plugins/ipc/ipc-activator.hpp"
#include "wayfire/core.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/window-manager.hpp"
#include "wayfire/seat.hpp"
#include "wayfire/nonstd/reverse.hpp"
#include "wm-actions-signals.hpp"
#include "wayfire/nonstd/reverse.hpp"

class always_on_top_root_node_t : public wf::scene::output_node_t
{
  public:
    using output_node_t::output_node_t;

    std::string stringify() const override
    {
        return "always-on-top for output " + get_output()->to_string() + " " + stringify_flags();
    }
};

class wayfire_wm_actions_output_t : public wf::per_output_plugin_instance_t
{
    wf::scene::floating_inner_ptr always_above;
    bool showdesktop_active = false;

    wf::option_wrapper_t<wf::activatorbinding_t> minimize{
        "wm-actions/minimize"};
    wf::option_wrapper_t<wf::activatorbinding_t> toggle_maximize{
        "wm-actions/toggle_maximize"};
    wf::option_wrapper_t<wf::activatorbinding_t> toggle_above{
        "wm-actions/toggle_always_on_top"};
    wf::option_wrapper_t<wf::activatorbinding_t> toggle_fullscreen{
        "wm-actions/toggle_fullscreen"};
    wf::option_wrapper_t<wf::activatorbinding_t> toggle_sticky{
        "wm-actions/toggle_sticky"};
    wf::option_wrapper_t<wf::activatorbinding_t> send_to_back{
        "wm-actions/send_to_back"};

    wf::plugin_activation_data_t grab_interface = {
        .name = "wm-actions",
        .capabilities = 0,
    };

  public:

    bool set_keep_above_state(wayfire_view view, bool above)
    {
        if (!view || !output->can_activate_plugin(&grab_interface))
        {
            return false;
        }

        if (above)
        {
            wf::scene::readd_front(always_above, view->get_root_node());
            view->store_data(std::make_unique<wf::custom_data_t>(),
                "wm-actions-above");
        } else
        {
            wf::scene::readd_front(output->wset()->get_node(), view->get_root_node());
            if (view->has_data("wm-actions-above"))
            {
                view->erase_data("wm-actions-above");
            }
        }

        wf::wm_actions_above_changed_signal data;
        data.view = view;
        output->emit(&data);
        return true;
    }

    /**
     * Find the selected toplevel view, or nullptr if the selected view is not
     * toplevel.
     */
    wayfire_toplevel_view choose_view(wf::activator_source_t source)
    {
        wayfire_view view;
        if (source == wf::activator_source_t::BUTTONBINDING)
        {
            view = wf::get_core().get_cursor_focus_view();
        } else
        {
            view = wf::get_core().seat->get_active_view();
        }

        return wf::toplevel_cast(view);
    }

    /**
     * Calling a specific view / specific keep_above action via signal
     */
    wf::signal::connection_t<wf::wm_actions_set_above_state_signal> on_set_above_state_signal =
        [=] (wf::wm_actions_set_above_state_signal *signal)
    {
        if (!set_keep_above_state(signal->view, signal->above))
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "view above action failed via signal.");
        }
    };

    /**
     * Ensures views marked as above are still above if their output changes.
     */
    wf::signal::connection_t<wf::view_moved_to_wset_signal> on_view_output_changed =
        [=] (wf::view_moved_to_wset_signal *signal)
    {
        if (!signal->new_wset || (signal->new_wset->get_attached_output() != output))
        {
            return;
        }

        auto view = signal->view;

        if (!view)
        {
            return;
        }

        if (view->has_data("wm-actions-above"))
        {
            wf::scene::readd_front(always_above, view->get_root_node());
        }
    };

    /**
     * Ensures views marked as above are still above if they are minimized and
     * unminimized.
     */
    wf::signal::connection_t<wf::view_minimized_signal> on_view_minimized =
        [=] (wf::view_minimized_signal *ev)
    {
        if (ev->view->get_output() != output)
        {
            return;
        }

        if (ev->view->has_data("wm-actions-above") && !ev->view->minimized)
        {
            wf::scene::readd_front(always_above, ev->view->get_root_node());
        }
    };

    void check_disable_showdesktop(wayfire_view view)
    {
        if ((view->role != wf::VIEW_ROLE_TOPLEVEL) || !view->is_mapped())
        {
            return;
        }

        disable_showdesktop();
    }

    /**
     * Disables show desktop if the workspace is changed or any view is attached,
     * mapped or unminimized.
     */
    wf::signal::connection_t<wf::view_set_output_signal> view_set_output =
        [=] (wf::view_set_output_signal *ev)
    {
        check_disable_showdesktop(ev->view);
    };

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        check_disable_showdesktop(ev->view);
    };

    wf::signal::connection_t<wf::workspace_changed_signal> workspace_changed =
        [=] (wf::workspace_changed_signal *ev)
    {
        disable_showdesktop();
    };

    wf::signal::connection_t<wf::view_minimized_signal> view_minimized = [=] (wf::view_minimized_signal *ev)
    {
        if ((ev->view->role != wf::VIEW_ROLE_TOPLEVEL) || !ev->view->is_mapped())
        {
            return;
        }

        if (!ev->view->minimized)
        {
            disable_showdesktop();
        }
    };

    /**
     * Execute for_view on the selected view, if available.
     */
    bool execute_for_selected_view(wf::activator_source_t source,
        std::function<bool(wayfire_toplevel_view)> for_view)
    {
        auto view = choose_view(source);
        if (!view || !output->can_activate_plugin(&grab_interface))
        {
            return false;
        }

        return for_view(view);
    }

    /**
     * The default activator bindings.
     */
    wf::activator_callback on_toggle_above = [=] (auto ev) -> bool
    {
        auto view = choose_view(ev.source);
        if (view)
        {
            return set_keep_above_state(view, !view->has_data("wm-actions-above"));
        } else
        {
            return false;
        }
    };

    wf::activator_callback on_minimize = [=] (auto ev) -> bool
    {
        return execute_for_selected_view(ev.source, [] (wayfire_toplevel_view view)
        {
            wf::get_core().default_wm->minimize_request(view, !view->minimized);
            return true;
        });
    };

    wf::activator_callback on_toggle_maximize = [=] (auto ev) -> bool
    {
        return execute_for_selected_view(ev.source, [] (wayfire_toplevel_view view)
        {
            wf::get_core().default_wm->tile_request(view,
                view->pending_tiled_edges() == wf::TILED_EDGES_ALL ? 0 : wf::TILED_EDGES_ALL);
            return true;
        });
    };

    wf::activator_callback on_toggle_fullscreen = [=] (auto ev) -> bool
    {
        return execute_for_selected_view(ev.source, [] (wayfire_toplevel_view view)
        {
            wf::get_core().default_wm->fullscreen_request(view, view->get_output(),
                !view->pending_fullscreen());
            return true;
        });
    };

    wf::activator_callback on_toggle_sticky = [=] (auto ev) -> bool
    {
        return execute_for_selected_view(ev.source, [] (wayfire_toplevel_view view)
        {
            view->set_sticky(view->sticky ^ 1);
            return true;
        });
    };

    bool on_toggle_showdesktop()
    {
        showdesktop_active = !showdesktop_active;

        if (showdesktop_active)
        {
            for (auto& view : output->wset()->get_views())
            {
                if (!view->minimized)
                {
                    wf::get_core().default_wm->minimize_request(view, true);
                    view->store_data(std::make_unique<wf::custom_data_t>(), "wm-actions-showdesktop");
                }
            }

            output->connect(&view_set_output);
            output->connect(&workspace_changed);
            output->connect(&view_minimized);
            output->connect(&on_view_mapped);
            return true;
        }

        disable_showdesktop();

        return true;
    }

    void do_send_to_back(wayfire_view view)
    {
        auto view_root = view->get_root_node();

        if (auto parent =
                dynamic_cast<wf::scene::floating_inner_node_t*>(view_root->parent()))
        {
            auto parent_children = parent->get_children();
            parent_children.erase(
                std::remove(parent_children.begin(), parent_children.end(),
                    view_root),
                parent_children.end());
            parent_children.push_back(view_root);
            parent->set_children_list(parent_children);
            wf::scene::update(parent->shared_from_this(),
                wf::scene::update_flag::CHILDREN_LIST);
        }
    }

    wf::activator_callback on_send_to_back = [=] (auto ev) -> bool
    {
        return execute_for_selected_view(ev.source, [this] (wayfire_view view)
        {
            auto views = view->get_output()->wset()->get_views(
                wf::WSET_CURRENT_WORKSPACE | wf::WSET_MAPPED_ONLY |
                wf::WSET_EXCLUDE_MINIMIZED | wf::WSET_SORT_STACKING);

            wayfire_view bottom_view = views[views.size() - 1];
            if (view != bottom_view)
            {
                do_send_to_back(view);
                // Change focus to the last focused view on this workspace

                // Update the list after restacking.
                views = view->get_output()->wset()->get_views(
                    wf::WSET_CURRENT_WORKSPACE | wf::WSET_MAPPED_ONLY |
                    wf::WSET_EXCLUDE_MINIMIZED | wf::WSET_SORT_STACKING);

                wf::get_core().seat->focus_view(views[0]);
            }

            return true;
        });
    };

    void disable_showdesktop()
    {
        view_set_output.disconnect();
        workspace_changed.disconnect();
        view_minimized.disconnect();

        auto views = output->wset()->get_views(wf::WSET_SORT_STACKING);
        for (auto& view : wf::reverse(views))
        {
            if (view->has_data("wm-actions-showdesktop"))
            {
                view->erase_data("wm-actions-showdesktop");
                wf::get_core().default_wm->minimize_request(view, false);
            }
        }

        showdesktop_active = false;
    }

  public:
    void init() override
    {
        always_above = std::make_shared<always_on_top_root_node_t>(output);
        wf::scene::add_front(wf::get_core().scene()->layers[(int)wf::scene::layer::WORKSPACE], always_above);
        output->add_activator(minimize, &on_minimize);
        output->add_activator(toggle_maximize, &on_toggle_maximize);
        output->add_activator(toggle_above, &on_toggle_above);
        output->add_activator(toggle_fullscreen, &on_toggle_fullscreen);
        output->add_activator(toggle_sticky, &on_toggle_sticky);
        output->add_activator(send_to_back, &on_send_to_back);
        output->connect(&on_set_above_state_signal);
        output->connect(&on_view_minimized);
        wf::get_core().connect(&on_view_output_changed);
    }

    void fini() override
    {
        for (auto view : output->wset()->get_views())
        {
            if (view->has_data("wm-actions-above"))
            {
                set_keep_above_state(view, false);
            }
        }

        wf::scene::remove_child(always_above);
        output->rem_binding(&on_minimize);
        output->rem_binding(&on_toggle_maximize);
        output->rem_binding(&on_toggle_above);
        output->rem_binding(&on_toggle_fullscreen);
        output->rem_binding(&on_toggle_sticky);
        output->rem_binding(&on_send_to_back);
    }
};

class wayfire_wm_actions_t : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_wm_actions_output_t>
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;
    wf::ipc_activator_t toggle_showdesktop{"wm-actions/toggle_showdesktop"};

  public:
    void init() override
    {
        init_output_tracking();
        ipc_repo->register_method("wm-actions/set-minimized", ipc_minimize);
        ipc_repo->register_method("wm-actions/set-always-on-top", ipc_set_always_on_top);
        ipc_repo->register_method("wm-actions/set-fullscreen", ipc_set_fullscreen);
        ipc_repo->register_method("wm-actions/set-sticky", ipc_set_sticky);
        ipc_repo->register_method("wm-actions/send-to-back", ipc_send_to_back);
        toggle_showdesktop.set_handler(on_toggle_showdesktop);
    }

    void fini() override
    {
        fini_output_tracking();
        ipc_repo->unregister_method("wm-actions/set-minimized");
        ipc_repo->unregister_method("wm-actions/set-always-on-top");
        ipc_repo->unregister_method("wm-actions/set-fullscreen");
        ipc_repo->unregister_method("wm-actions/set-sticky");
        ipc_repo->unregister_method("wm-actions/send-to-back");
    }

    wf::json_t execute_for_view(const wf::json_t& params,
        std::function<void(wayfire_toplevel_view, bool)> view_op)
    {
        uint64_t view_id = wf::ipc::json_get_uint64(params, "view_id");
        bool state = wf::ipc::json_get_bool(params, "state");
        wayfire_toplevel_view view = toplevel_cast(wf::ipc::find_view_by_id(view_id));
        if (!view)
        {
            return wf::ipc::json_error("toplevel view id not found!");
        }

        view_op(view, state);
        return wf::ipc::json_ok();
    }

    wf::ipc::method_callback ipc_minimize = [=] (const wf::json_t& js)
    {
        return execute_for_view(js, [=] (wayfire_toplevel_view view, bool state)
        {
            wf::get_core().default_wm->minimize_request(view, state);
        });
    };

    wf::ipc::method_callback ipc_maximize = [=] (const wf::json_t& js)
    {
        return execute_for_view(js, [=] (wayfire_toplevel_view view, bool state)
        {
            wf::get_core().default_wm->tile_request(view, state ? wf::TILED_EDGES_ALL : 0);
        });
    };

    wf::ipc::method_callback ipc_set_always_on_top = [=] (const wf::json_t& js)
    {
        return execute_for_view(js, [=] (wayfire_toplevel_view view, bool state)
        {
            if (!view->get_output())
            {
                view->store_data(std::make_unique<wf::custom_data_t>(), "wm-actions-above");
                return;
            }

            output_instance[view->get_output()]->set_keep_above_state(view, state);
        });
    };

    wf::ipc::method_callback ipc_set_fullscreen = [=] (const wf::json_t& js)
    {
        return execute_for_view(js, [=] (wayfire_toplevel_view view, bool state)
        {
            wf::get_core().default_wm->fullscreen_request(view, nullptr, state);
        });
    };

    wf::ipc::method_callback ipc_set_sticky = [=] (const wf::json_t& js)
    {
        return execute_for_view(js, [=] (wayfire_toplevel_view view, bool state)
        {
            view->set_sticky(state);
        });
    };

    wf::ipc::method_callback ipc_send_to_back = [=] (const wf::json_t& js)
    {
        return execute_for_view(js, [=] (wayfire_toplevel_view view, bool state)
        {
            if (!view->get_output())
            {
                return;
            }

            output_instance[view->get_output()]->do_send_to_back(view);
        });
    };

    wf::ipc_activator_t::handler_t on_toggle_showdesktop = [=] (wf::output_t *output, wayfire_view)
    {
        return this->output_instance[output]->on_toggle_showdesktop();
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_wm_actions_t);
