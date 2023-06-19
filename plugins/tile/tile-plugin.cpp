#include <memory>
#include <wayfire/config/types.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>

#include "tree-controller.hpp"
#include "tree.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/object.hpp"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/plugins/common/input-grab.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/txn/transaction-manager.hpp"

struct autocommit_transaction_t
{
  public:
    wf::txn::transaction_uptr tx;
    autocommit_transaction_t()
    {
        tx = wf::txn::transaction_t::create();
    }

    ~autocommit_transaction_t()
    {
        if (!tx->get_objects().empty())
        {
            wf::get_core().tx_manager->schedule_transaction(std::move(tx));
        }
    }
};

namespace wf
{
static bool can_tile_view(wayfire_toplevel_view view)
{
    if (view->parent)
    {
        return false;
    }

    return true;
}

/**
 * When a view is moved from one output to the other, we want to keep its tiled
 * status. To achieve this, we do the following:
 *
 * 1. In view-pre-moved-to-output handler, we set view_auto_tile_t custom data.
 * 2. In detach handler, we just remove the view as usual.
 * 3. We now know we will receive attach as next event.
 *    Check for view_auto_tile_t, and tile the view again.
 */
class view_auto_tile_t : public wf::custom_data_t
{};

class tile_workspace_set_data_t : public wf::custom_data_t
{
  public:
    std::vector<std::vector<std::unique_ptr<wf::tile::tree_node_t>>> roots;
    std::vector<std::vector<wf::scene::floating_inner_ptr>> tiled_sublayer;
    const wf::tile::split_direction_t default_split = wf::tile::SPLIT_VERTICAL;

    wf::option_wrapper_t<int> inner_gaps{"simple-tile/inner_gap_size"};
    wf::option_wrapper_t<int> outer_horiz_gaps{"simple-tile/outer_horiz_gap_size"};
    wf::option_wrapper_t<int> outer_vert_gaps{"simple-tile/outer_vert_gap_size"};

    tile_workspace_set_data_t(std::shared_ptr<wf::workspace_set_t> wset)
    {
        this->wset = wset;
        wset->connect(&on_wset_attached);
        wset->connect(&on_workspace_grid_changed);
        resize_roots(wset->get_workspace_grid_size());

        inner_gaps.set_callback(update_gaps);
        outer_horiz_gaps.set_callback(update_gaps);
        outer_vert_gaps.set_callback(update_gaps);
    }

    wf::signal::connection_t<workarea_changed_signal> on_workarea_changed = [=] (auto)
    {
        update_root_size();
    };

    wf::signal::connection_t<workspace_set_attached_signal> on_wset_attached = [=] (auto)
    {
        on_workarea_changed.disconnect();
        if (wset.lock()->get_attached_output())
        {
            wset.lock()->get_attached_output()->connect(&on_workarea_changed);
            update_root_size();
        }
    };

    wf::signal::connection_t<wf::workspace_grid_changed_signal> on_workspace_grid_changed = [=] (auto)
    {
        wf::dassert(!wset.expired(), "wset should not expire, ever!");
        resize_roots(wset.lock()->get_workspace_grid_size());
    };

    void resize_roots(wf::dimensions_t wsize)
    {
        for (size_t i = 0; i < tiled_sublayer.size(); i++)
        {
            for (size_t j = 0; j < tiled_sublayer[i].size(); j++)
            {
                if (wset.lock()->is_workspace_valid({(int)i, (int)j}))
                {
                    destroy_sublayer(tiled_sublayer[i][j]);
                }
            }
        }

        roots.resize(wsize.width);
        tiled_sublayer.resize(wsize.width);
        for (int i = 0; i < wsize.width; i++)
        {
            roots[i].resize(wsize.height);
            tiled_sublayer[i].resize(wsize.height);
            for (int j = 0; j < wsize.height; j++)
            {
                roots[i][j] = std::make_unique<wf::tile::split_node_t>(default_split);
                tiled_sublayer[i][j] = std::make_shared<wf::scene::floating_inner_node_t>(false);
                wf::scene::add_front(wset.lock()->get_node(), tiled_sublayer[i][j]);
            }
        }

        update_root_size();
        update_gaps();
    }

    void update_root_size()
    {
        auto wo = wset.lock()->get_attached_output();
        wf::geometry_t workarea = wo ? wo->workarea->get_workarea() : tile::default_output_resolution;

        wf::geometry_t output_geometry =
            wset.lock()->get_last_output_geometry().value_or(tile::default_output_resolution);

        auto wsize = wset.lock()->get_workspace_grid_size();
        for (int i = 0; i < wsize.width; i++)
        {
            for (int j = 0; j < wsize.height; j++)
            {
                /* Set size */
                auto vp_geometry = workarea;
                vp_geometry.x += i * output_geometry.width;
                vp_geometry.y += j * output_geometry.height;

                autocommit_transaction_t tx;
                roots[i][j]->set_geometry(vp_geometry, tx.tx);
            }
        }
    }

    void destroy_sublayer(wf::scene::floating_inner_ptr sublayer)
    {
        // Transfer views to the top
        auto root     = wset.lock()->get_node();
        auto children = root->get_children();
        auto sublayer_children = sublayer->get_children();
        sublayer->set_children_list({});
        children.insert(children.end(), sublayer_children.begin(), sublayer_children.end());
        root->set_children_list(children);
        wf::scene::update(root, wf::scene::update_flag::CHILDREN_LIST);
        wf::scene::remove_child(sublayer);
    }

    std::function<void()> update_gaps = [=] ()
    {
        tile::gap_size_t gaps = {
            .left   = outer_horiz_gaps,
            .right  = outer_horiz_gaps,
            .top    = outer_vert_gaps,
            .bottom = outer_vert_gaps,
            .internal = inner_gaps,
        };

        for (auto& col : roots)
        {
            for (auto& root : col)
            {
                autocommit_transaction_t tx;
                root->set_gaps(gaps, tx.tx);
                root->set_geometry(root->geometry, tx.tx);
            }
        }
    };

    void flatten_roots()
    {
        for (auto& col : roots)
        {
            for (auto& root : col)
            {
                autocommit_transaction_t tx;
                tile::flatten_tree(root, tx.tx);
            }
        }
    }

    static tile_workspace_set_data_t& get(std::shared_ptr<workspace_set_t> set)
    {
        if (!set->has_data<tile_workspace_set_data_t>())
        {
            set->store_data(std::make_unique<tile_workspace_set_data_t>(set));
        }

        return *set->get_data<tile_workspace_set_data_t>();
    }

    static tile_workspace_set_data_t& get(wf::output_t *output)
    {
        return get(output->wset());
    }

    static std::unique_ptr<tile::tree_node_t>& get_current_root(wf::output_t *output)
    {
        auto set   = output->wset();
        auto vp    = set->get_current_workspace();
        auto& data = get(output);
        return data.roots[vp.x][vp.y];
    }

    static scene::floating_inner_ptr get_current_sublayer(wf::output_t *output)
    {
        auto set   = output->wset();
        auto vp    = set->get_current_workspace();
        auto& data = get(output);
        return data.tiled_sublayer[vp.x][vp.y];
    }

    std::weak_ptr<workspace_set_t> wset;

    void attach_view(wayfire_toplevel_view view, wf::point_t vp = {-1, -1})
    {
        view->set_allowed_actions(VIEW_ALLOW_WS_CHANGE);

        if (vp == wf::point_t{-1, -1})
        {
            vp = wset.lock()->get_current_workspace();
        }

        auto view_node = std::make_unique<wf::tile::view_node_t>(view);
        {
            autocommit_transaction_t tx;
            roots[vp.x][vp.y]->as_split_node()->add_child(std::move(view_node), tx.tx);
        }

        auto node = view->get_root_node();
        wf::scene::readd_front(tiled_sublayer[vp.x][vp.y], node);
        view_bring_to_front(view);
    }

    /** Remove the given view from its tiling container */
    void detach_view(nonstd::observer_ptr<tile::view_node_t> view,
        bool reinsert = true)
    {
        auto wview = view->view;
        wview->set_allowed_actions(VIEW_ALLOW_ALL);
        {
            autocommit_transaction_t tx;
            view->parent->remove_child(view, tx.tx);
        }
        /* View node is invalid now */
        flatten_roots();

        if (wview->pending_fullscreen() && wview->is_mapped())
        {
            wf::get_core().default_wm->fullscreen_request(wview, nullptr, false);
        }

        /* Remove from special sublayer */
        if (reinsert)
        {
            wf::scene::readd_front(wview->get_output()->wset()->get_node(), wview->get_root_node());
        }
    }
};

class tile_output_plugin_t : public wf::pointer_interaction_t, public wf::custom_data_t
{
  private:
    wf::view_matcher_t tile_by_default{"simple-tile/tile_by_default"};
    wf::option_wrapper_t<bool> keep_fullscreen_on_adjacent{"simple-tile/keep_fullscreen_on_adjacent"};
    wf::option_wrapper_t<wf::buttonbinding_t> button_move{"simple-tile/button_move"};
    wf::option_wrapper_t<wf::buttonbinding_t> button_resize{"simple-tile/button_resize"};

    wf::option_wrapper_t<wf::keybinding_t> key_toggle_tile{"simple-tile/key_toggle"};
    wf::option_wrapper_t<wf::keybinding_t> key_focus_left{"simple-tile/key_focus_left"};
    wf::option_wrapper_t<wf::keybinding_t> key_focus_right{"simple-tile/key_focus_right"};
    wf::option_wrapper_t<wf::keybinding_t> key_focus_above{"simple-tile/key_focus_above"};
    wf::option_wrapper_t<wf::keybinding_t> key_focus_below{"simple-tile/key_focus_below"};
    wf::output_t *output;

  public:
    std::unique_ptr<wf::input_grab_t> input_grab;

    static std::unique_ptr<wf::tile::tile_controller_t> get_default_controller()
    {
        return std::make_unique<wf::tile::tile_controller_t>();
    }

    std::unique_ptr<wf::tile::tile_controller_t> controller =
        get_default_controller();

    /**
     * Translate coordinates from output-local coordinates to the coordinate
     * system of the tiling trees, depending on the current workspace
     */
    wf::point_t get_global_input_coordinates()
    {
        wf::pointf_t local = output->get_cursor_position();

        auto vp   = output->wset()->get_current_workspace();
        auto size = output->get_screen_size();
        local.x += size.width * vp.x;
        local.y += size.height * vp.y;

        return {(int)local.x, (int)local.y};
    }

    /** Check whether we currently have a fullscreen tiled view */
    bool has_fullscreen_view()
    {
        int count_fullscreen = 0;
        for_each_view(tile_workspace_set_data_t::get_current_root(output), [&] (wayfire_toplevel_view view)
        {
            count_fullscreen += view->pending_fullscreen();
        });

        return count_fullscreen > 0;
    }

    /** Check whether the current pointer focus is tiled view */
    bool has_tiled_focus()
    {
        auto focus = wf::get_core().get_cursor_focus_view();

        return focus && tile::view_node_t::get_node(focus);
    }

    template<class Controller>
    void start_controller()
    {
        /* No action possible in this case */
        if (has_fullscreen_view() || !has_tiled_focus())
        {
            return;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return;
        }

        input_grab->grab_input(wf::scene::layer::OVERLAY);
        controller = std::make_unique<Controller>(tile_workspace_set_data_t::get_current_root(output),
            get_global_input_coordinates());
    }

    void stop_controller(bool force_stop)
    {
        if (!output->is_plugin_active(grab_interface.name))
        {
            return;
        }

        input_grab->ungrab_input();

        // Deactivate plugin, so that others can react to the events
        output->deactivate_plugin(&grab_interface);
        if (!force_stop)
        {
            controller->input_released();
        }

        controller = get_default_controller();
    }

    bool tile_window_by_default(wayfire_toplevel_view view)
    {
        return tile_by_default.matches(view) && can_tile_view(view);
    }

    void attach_view(wayfire_toplevel_view view, wf::point_t vp = {-1, -1})
    {
        if (!view->get_wset())
        {
            return;
        }

        stop_controller(true);
        tile_workspace_set_data_t::get(view->get_wset()).attach_view(view, vp);
    }

    void detach_view(nonstd::observer_ptr<tile::view_node_t> view, bool reinsert = true)
    {
        stop_controller(true);
        tile_workspace_set_data_t::get(view->view->get_wset()).detach_view(view, reinsert);
    }

    wf::signal::connection_t<view_mapped_signal> on_view_mapped = [=] (view_mapped_signal *ev)
    {
        if (auto toplevel = toplevel_cast(ev->view))
        {
            if (tile_window_by_default(toplevel))
            {
                stop_controller(true);
                tile_workspace_set_data_t::get(output).attach_view(toplevel);
            }
        }
    };

    wf::signal::connection_t<view_unmapped_signal> on_view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        stop_controller(true);
        auto node = wf::tile::view_node_t::get_node(ev->view);
        if (node)
        {
            detach_view(node);
        }
    };

    wf::signal::connection_t<view_tile_request_signal> on_tile_request = [=] (view_tile_request_signal *ev)
    {
        if (ev->carried_out || !tile::view_node_t::get_node(ev->view))
        {
            return;
        }

        // we ignore those requests because we manage the tiled state manually
        ev->carried_out = true;
    };

    void set_view_fullscreen(wayfire_toplevel_view view, bool fullscreen)
    {
        /* Set fullscreen, and trigger resizing of the views (which will commit the view) */
        view->toplevel()->pending().fullscreen = fullscreen;
        tile_workspace_set_data_t::get(output).update_root_size();
    }

    wf::signal::connection_t<view_fullscreen_request_signal> on_fullscreen_request =
        [=] (view_fullscreen_request_signal *ev)
    {
        if (ev->carried_out || !tile::view_node_t::get_node(ev->view))
        {
            return;
        }

        ev->carried_out = true;
        set_view_fullscreen(ev->view, ev->state);
    };

    wf::signal::connection_t<focus_view_signal> on_focus_changed = [=] (focus_view_signal *ev)
    {
        if (!toplevel_cast(ev->view))
        {
            return;
        }

        auto toplevel = toplevel_cast(ev->view);
        if (tile::view_node_t::get_node(ev->view) && !toplevel->pending_fullscreen())
        {
            for_each_view(tile_workspace_set_data_t::get_current_root(output), [&] (
                wayfire_toplevel_view view)
            {
                if (view->pending_fullscreen())
                {
                    set_view_fullscreen(view, false);
                }
            });
        }
    };

    void change_view_workspace(wayfire_toplevel_view view, wf::point_t vp = {-1, -1})
    {
        auto existing_node = wf::tile::view_node_t::get_node(view);
        if (existing_node)
        {
            detach_view(existing_node);
            attach_view(view, vp);
        }
    }

    wf::signal::connection_t<view_change_workspace_signal> on_view_change_workspace =
        [=] (view_change_workspace_signal *ev)
    {
        if (ev->old_workspace_valid)
        {
            change_view_workspace(ev->view, ev->to);
        }
    };

    wf::signal::connection_t<view_minimized_signal> on_view_minimized = [=] (view_minimized_signal *ev)
    {
        auto existing_node = wf::tile::view_node_t::get_node(ev->view);

        if (ev->view->minimized && existing_node)
        {
            detach_view(existing_node);
        }

        if (!ev->view->minimized && tile_window_by_default(ev->view))
        {
            attach_view(ev->view);
        }
    };

    /**
     * Execute the given function on the focused view iff we can activate the
     * tiling plugin, there is a focused view and the focused view is a tiled
     * view
     *
     * @param need_tiled Whether the view needs to be tiled
     */
    bool conditioned_view_execute(bool need_tiled,
        std::function<void(wayfire_toplevel_view)> func)
    {
        auto view = output->get_active_view();
        if (!toplevel_cast(view))
        {
            return false;
        }

        if (need_tiled && !tile::view_node_t::get_node(view))
        {
            return false;
        }

        if (output->can_activate_plugin(&grab_interface))
        {
            func(toplevel_cast(view));
            return true;
        }

        return false;
    }

    wf::key_callback on_toggle_tiled_state = [=] (auto)
    {
        return conditioned_view_execute(false, [=] (wayfire_toplevel_view view)
        {
            auto existing_node = tile::view_node_t::get_node(view);
            if (existing_node)
            {
                detach_view(existing_node);
                wf::get_core().default_wm->tile_request(view, 0);
            } else
            {
                attach_view(view);
            }
        });
    };

    bool focus_adjacent(tile::split_insertion_t direction)
    {
        return conditioned_view_execute(true, [=] (wayfire_toplevel_view view)
        {
            auto adjacent = tile::find_first_view_in_direction(
                tile::view_node_t::get_node(view), direction);

            bool was_fullscreen = view->pending_fullscreen();
            if (adjacent)
            {
                /* This will lower the fullscreen status of the view */
                output->focus_view(adjacent->view, true);

                if (was_fullscreen && keep_fullscreen_on_adjacent)
                {
                    wf::get_core().default_wm->fullscreen_request(adjacent->view, output, true);
                }
            }
        });
    }

    wf::key_callback on_focus_adjacent = [=] (wf::keybinding_t binding)
    {
        if (binding == key_focus_left)
        {
            return focus_adjacent(tile::INSERT_LEFT);
        }

        if (binding == key_focus_right)
        {
            return focus_adjacent(tile::INSERT_RIGHT);
        }

        if (binding == key_focus_above)
        {
            return focus_adjacent(tile::INSERT_ABOVE);
        }

        if (binding == key_focus_below)
        {
            return focus_adjacent(tile::INSERT_BELOW);
        }

        return false;
    };

    wf::button_callback on_move_view = [=] (auto)
    {
        start_controller<tile::move_view_controller_t>();
        return false; // pass button to the grab node
    };

    wf::button_callback on_resize_view = [=] (auto)
    {
        start_controller<tile::resize_view_controller_t>();
        return false; // pass button to the grab node
    };

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        if (event.state == WLR_BUTTON_RELEASED)
        {
            stop_controller(false);
        }
    }

    void handle_pointer_motion(wf::pointf_t pointer_position, uint32_t time_ms) override
    {
        controller->input_motion(get_global_input_coordinates());
    }

    void setup_callbacks()
    {
        output->add_button(button_move, &on_move_view);
        output->add_button(button_resize, &on_resize_view);
        output->add_key(key_toggle_tile, &on_toggle_tiled_state);

        output->add_key(key_focus_left, &on_focus_adjacent);
        output->add_key(key_focus_right, &on_focus_adjacent);
        output->add_key(key_focus_above, &on_focus_adjacent);
        output->add_key(key_focus_below, &on_focus_adjacent);
    }

    wf::plugin_activation_data_t grab_interface = {
        .name = "simple-tile",
        .capabilities = CAPABILITY_MANAGE_COMPOSITOR,
    };

  public:
    tile_output_plugin_t(wf::output_t *wo)
    {
        this->output = wo;
        input_grab   = std::make_unique<wf::input_grab_t>("simple-tile", output, nullptr, this, nullptr);
        output->connect(&on_view_mapped);
        output->connect(&on_view_unmapped);
        output->connect(&on_tile_request);
        output->connect(&on_fullscreen_request);
        output->connect(&on_focus_changed);
        output->connect(&on_view_change_workspace);
        output->connect(&on_view_minimized);
        setup_callbacks();
    }

    ~tile_output_plugin_t()
    {
        output->rem_binding(&on_move_view);
        output->rem_binding(&on_resize_view);
        output->rem_binding(&on_toggle_tiled_state);
        output->rem_binding(&on_focus_adjacent);
    }
};

class tile_plugin_t : public wf::plugin_interface_t, wf::per_output_tracker_mixin_t<>
{
  public:
    void init() override
    {
        init_output_tracking();
        wf::get_core().connect(&on_view_pre_moved_to_wset);
        wf::get_core().connect(&on_view_moved_to_wset);
    }

    void fini() override
    {
        fini_output_tracking();
        for (auto wset : workspace_set_t::get_all())
        {
            wset->erase_data<tile_workspace_set_data_t>();
        }
    }

    wf::signal::connection_t<view_pre_moved_to_wset_signal> on_view_pre_moved_to_wset =
        [=] (view_pre_moved_to_wset_signal *ev)
    {
        auto node = wf::tile::view_node_t::get_node(ev->view);
        if (node)
        {
            ev->view->store_data(std::make_unique<wf::view_auto_tile_t>());
            if (ev->old_wset)
            {
                if (auto wo = ev->old_wset->get_attached_output())
                {
                    wo->get_data<tile_output_plugin_t>()->stop_controller(true);
                }

                tile_workspace_set_data_t::get(ev->old_wset).detach_view(node);
            }
        }
    };

    wf::signal::connection_t<view_moved_to_wset_signal> on_view_moved_to_wset =
        [=] (view_moved_to_wset_signal *ev)
    {
        if (ev->view->has_data<view_auto_tile_t>() && ev->new_wset)
        {
            if (auto wo = ev->new_wset->get_attached_output())
            {
                wo->get_data<tile_output_plugin_t>()->stop_controller(true);
            }

            tile_workspace_set_data_t::get(ev->new_wset).attach_view(ev->view);
        }
    };

    void handle_new_output(wf::output_t *output) override
    {
        output->store_data(std::make_unique<tile_output_plugin_t>(output));
    }

    void handle_output_removed(wf::output_t *output) override
    {
        output->erase_data<tile_output_plugin_t>();
    }
};
}

DECLARE_WAYFIRE_PLUGIN(wf::tile_plugin_t);
