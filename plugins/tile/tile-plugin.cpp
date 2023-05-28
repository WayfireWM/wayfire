#include <memory>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

#include "tree-controller.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/plugins/common/input-grab.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/view-helpers.hpp"

namespace wf
{
class tile_workspace_implementation_t : public wf::workspace_implementation_t
{
  public:
    bool view_movable(wayfire_view view) override
    {
        return wf::tile::view_node_t::get_node(view) == nullptr;
    }

    bool view_resizable(wayfire_view view) override
    {
        return wf::tile::view_node_t::get_node(view) == nullptr;
    }
};

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

class tile_plugin_t : public wf::per_output_plugin_instance_t, public wf::pointer_interaction_t
{
  private:
    wf::view_matcher_t tile_by_default{"simple-tile/tile_by_default"};
    wf::option_wrapper_t<bool> keep_fullscreen_on_adjacent{
        "simple-tile/keep_fullscreen_on_adjacent"};
    wf::option_wrapper_t<wf::buttonbinding_t> button_move{"simple-tile/button_move"},
    button_resize{"simple-tile/button_resize"};
    wf::option_wrapper_t<wf::keybinding_t> key_toggle_tile{"simple-tile/key_toggle"};

    wf::option_wrapper_t<wf::keybinding_t> key_focus_left{
        "simple-tile/key_focus_left"},
    key_focus_right{"simple-tile/key_focus_right"};
    wf::option_wrapper_t<wf::keybinding_t> key_focus_above{
        "simple-tile/key_focus_above"},
    key_focus_below{"simple-tile/key_focus_below"};

    wf::option_wrapper_t<int> inner_gaps{"simple-tile/inner_gap_size"};
    wf::option_wrapper_t<int> outer_horiz_gaps{"simple-tile/outer_horiz_gap_size"};
    wf::option_wrapper_t<int> outer_vert_gaps{"simple-tile/outer_vert_gap_size"};

  private:
    std::unique_ptr<wf::input_grab_t> input_grab;
    std::vector<std::vector<std::unique_ptr<wf::tile::tree_node_t>>> roots;
    std::vector<std::vector<wf::scene::floating_inner_ptr>> tiled_sublayer;

    const wf::tile::split_direction_t default_split = wf::tile::SPLIT_VERTICAL;

    wf::signal::connection_t<wf::workspace_grid_changed_signal> on_workspace_grid_changed = [=] (auto)
    {
        resize_roots(output->workspace->get_workspace_grid_size());
    };

    void resize_roots(wf::dimensions_t wsize)
    {
        for (size_t i = 0; i < tiled_sublayer.size(); i++)
        {
            for (size_t j = 0; j < tiled_sublayer[i].size(); j++)
            {
                if (!output->workspace->is_workspace_valid({(int)i, (int)j}))
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
                roots[i][j] =
                    std::make_unique<wf::tile::split_node_t>(default_split);

                tiled_sublayer[i][j] =
                    std::make_shared<wf::scene::floating_inner_node_t>(false);

                wf::scene::add_front(output->workspace->get_node(), tiled_sublayer[i][j]);
            }
        }

        update_root_size(output->workarea->get_workarea());
    }

    void update_root_size(wf::geometry_t workarea)
    {
        auto output_geometry = output->get_relative_geometry();
        auto wsize = output->workspace->get_workspace_grid_size();
        for (int i = 0; i < wsize.width; i++)
        {
            for (int j = 0; j < wsize.height; j++)
            {
                /* Set size */
                auto vp_geometry = workarea;
                vp_geometry.x += i * output_geometry.width;
                vp_geometry.y += j * output_geometry.height;
                roots[i][j]->set_geometry(vp_geometry);
            }
        }
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
                root->set_gaps(gaps);
                root->set_geometry(root->geometry);
            }
        }
    };

    void flatten_roots()
    {
        for (auto& col : roots)
        {
            for (auto& root : col)
            {
                tile::flatten_tree(root);
            }
        }
    }

    bool can_tile_view(wayfire_view view)
    {
        if (view->role != wf::VIEW_ROLE_TOPLEVEL)
        {
            return false;
        }

        if (view->parent)
        {
            return false;
        }

        return true;
    }

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

        auto vp   = output->workspace->get_current_workspace();
        auto size = output->get_screen_size();
        local.x += size.width * vp.x;
        local.y += size.height * vp.y;

        return {(int)local.x, (int)local.y};
    }

    /** Check whether we currently have a fullscreen tiled view */
    bool has_fullscreen_view()
    {
        auto vp = output->workspace->get_current_workspace();

        int count_fullscreen = 0;
        for_each_view(roots[vp.x][vp.y], [&] (wayfire_view view)
        {
            count_fullscreen += view->fullscreen;
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

        input_grab->grab_input(wf::scene::layer::OVERLAY, true);
        auto vp = output->workspace->get_current_workspace();
        controller = std::make_unique<Controller>(roots[vp.x][vp.y], get_global_input_coordinates());
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

    void attach_view(wayfire_view view, wf::point_t vp = {-1, -1})
    {
        if (!can_tile_view(view))
        {
            return;
        }

        stop_controller(true);

        if (vp == wf::point_t{-1, -1})
        {
            vp = output->workspace->get_current_workspace();
        }

        auto view_node = std::make_unique<wf::tile::view_node_t>(view);
        roots[vp.x][vp.y]->as_split_node()->add_child(std::move(view_node));

        auto node = view->get_root_node();
        wf::scene::readd_front(tiled_sublayer[vp.x][vp.y], node);
        view_bring_to_front(view);
    }

    bool tile_window_by_default(wayfire_view view)
    {
        return tile_by_default.matches(view) && can_tile_view(view);
    }

    wf::signal::connection_t<view_mapped_signal> on_view_mapped = [=] (view_mapped_signal *ev)
    {
        if (tile_window_by_default(ev->view))
        {
            attach_view(ev->view);
        }
    };

    wf::signal::connection_t<view_set_output_signal> on_view_set_output = [=] (view_set_output_signal *ev)
    {
        if (ev->view->has_data<view_auto_tile_t>())
        {
            attach_view(ev->view);
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

    wf::signal::connection_t<view_pre_moved_to_output_signal> on_view_pre_moved_to_output =
        [=] (view_pre_moved_to_output_signal *ev)
    {
        auto node = wf::tile::view_node_t::get_node(ev->view);
        if ((ev->new_output == this->output) && node)
        {
            ev->view->store_data(std::make_unique<wf::view_auto_tile_t>());
            detach_view(node);
        }
    };

    /** Remove the given view from its tiling container */
    void detach_view(nonstd::observer_ptr<tile::view_node_t> view,
        bool reinsert = true)
    {
        stop_controller(true);
        auto wview = view->view;

        view->parent->remove_child(view);
        /* View node is invalid now */
        flatten_roots();

        if (wview->fullscreen && wview->is_mapped())
        {
            wview->fullscreen_request(nullptr, false);
        }

        /* Remove from special sublayer */
        if (reinsert)
        {
            wf::scene::readd_front(wview->get_output()->workspace->get_node(), wview->get_root_node());
        }
    }

    wf::signal::connection_t<workarea_changed_signal> on_workarea_changed = [=] (auto)
    {
        update_root_size(output->workarea->get_workarea());
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

    void set_view_fullscreen(wayfire_view view, bool fullscreen)
    {
        /* Set fullscreen, and trigger resizing of the views */
        view->set_fullscreen(fullscreen);
        update_root_size(output->workarea->get_workarea());
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
        if (ev->view && tile::view_node_t::get_node(ev->view) && !ev->view->fullscreen)
        {
            auto vp = output->workspace->get_current_workspace();
            for_each_view(roots[vp.x][vp.y], [&] (wayfire_view view)
            {
                if (view->fullscreen)
                {
                    set_view_fullscreen(view, false);
                }
            });
        }
    };

    void change_view_workspace(wayfire_view view, wf::point_t vp = {-1, -1})
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
        std::function<void(wayfire_view)> func)
    {
        auto view = output->get_active_view();
        if (!view)
        {
            return false;
        }

        if (need_tiled && !tile::view_node_t::get_node(view))
        {
            return false;
        }

        if (output->can_activate_plugin(&grab_interface))
        {
            func(view);
            return true;
        }

        return false;
    }

    wf::key_callback on_toggle_tiled_state = [=] (auto)
    {
        return conditioned_view_execute(false, [=] (wayfire_view view)
        {
            auto existing_node = tile::view_node_t::get_node(view);
            if (existing_node)
            {
                detach_view(existing_node);
                view->tile_request(0);
            } else
            {
                attach_view(view);
            }
        });
    };

    bool focus_adjacent(tile::split_insertion_t direction)
    {
        return conditioned_view_execute(true, [=] (wayfire_view view)
        {
            auto adjacent = tile::find_first_view_in_direction(
                tile::view_node_t::get_node(view), direction);

            bool was_fullscreen = view->fullscreen;
            if (adjacent)
            {
                /* This will lower the fullscreen status of the view */
                output->focus_view(adjacent->view, true);

                if (was_fullscreen && keep_fullscreen_on_adjacent)
                {
                    adjacent->view->fullscreen_request(output, true);
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
        return false;
    };

    wf::button_callback on_resize_view = [=] (auto)
    {
        start_controller<tile::resize_view_controller_t>();
        return false;
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

        inner_gaps.set_callback(update_gaps);
        outer_horiz_gaps.set_callback(update_gaps);
        outer_vert_gaps.set_callback(update_gaps);
        update_gaps();
    }

    wf::plugin_activation_data_t grab_interface = {
        .name = "simple-tile",
        .capabilities = CAPABILITY_MANAGE_COMPOSITOR,
    };

  public:
    void init() override
    {
        input_grab = std::make_unique<wf::input_grab_t>("simple-tile", output, nullptr, this, nullptr);
        resize_roots(output->workspace->get_workspace_grid_size());
        // TODO: check whether this was successful
        output->workspace->set_workspace_implementation(
            std::make_unique<tile_workspace_implementation_t>(), true);

        output->connect(&on_view_mapped);
        output->connect(&on_view_unmapped);
        output->connect(&on_view_set_output);
        output->connect(&on_workarea_changed);
        output->connect(&on_tile_request);
        output->connect(&on_fullscreen_request);
        output->connect(&on_focus_changed);
        output->connect(&on_view_change_workspace);
        output->connect(&on_view_minimized);
        output->connect(&on_workspace_grid_changed);
        wf::get_core().connect(&on_view_pre_moved_to_output);

        setup_callbacks();
    }

    void destroy_sublayer(wf::scene::floating_inner_ptr sublayer)
    {
        // Transfer views to the top
        auto root     = output->workspace->get_node();
        auto children = root->get_children();
        auto sublayer_children = sublayer->get_children();
        sublayer->set_children_list({});
        children.insert(children.end(),
            sublayer_children.begin(), sublayer_children.end());
        root->set_children_list(children);
        wf::scene::update(root,
            wf::scene::update_flag::CHILDREN_LIST);

        wf::scene::remove_child(sublayer);
    }

    void fini() override
    {
        output->workspace->set_workspace_implementation(nullptr, true);

        for (auto& row : tiled_sublayer)
        {
            for (auto& sublayer : row)
            {
                destroy_sublayer(sublayer);
            }
        }

        output->rem_binding(&on_move_view);
        output->rem_binding(&on_resize_view);
        output->rem_binding(&on_toggle_tiled_state);
        output->rem_binding(&on_focus_adjacent);
    }
};
}

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf::tile_plugin_t>);
