#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/debug.hpp>

#include <wayfire/plugins/common/workspace-wall.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>
#include <wayfire/plugins/common/move-drag-interface.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/common/key-repeat.hpp>

/* TODO: this file should be included in some header maybe(plugin.hpp) */
#include <linux/input-event-codes.h>

class wayfire_expo : public wf::plugin_interface_t
{
  private:
    wf::point_t convert_workspace_index_to_coords(int index)
    {
        index--; // compensate for indexing from 0
        auto wsize = output->workspace->get_workspace_grid_size();
        int x = index % wsize.width;
        int y = index / wsize.width;

        return wf::point_t{x, y};
    }

    wf::activator_callback toggle_cb = [=] (auto)
    {
        if (!state.active)
        {
            return activate();
        } else
        {
            if (!zoom_animation.running() || state.zoom_in)
            {
                deactivate();

                return true;
            }
        }

        return false;
    };

    wf::option_wrapper_t<wf::activatorbinding_t> toggle_binding{"expo/toggle"};
    wf::option_wrapper_t<wf::color_t> background_color{"expo/background"};
    wf::option_wrapper_t<int> zoom_duration{"expo/duration"};
    wf::option_wrapper_t<int> delimiter_offset{"expo/offset"};
    wf::option_wrapper_t<bool> keyboard_interaction{"expo/keyboard_interaction"};
    wf::option_wrapper_t<double> inactive_brightness{"expo/inactive_brightness"};
    wf::option_wrapper_t<int> transition_length{"expo/transition_length"};
    wf::geometry_animation_t zoom_animation{zoom_duration};

    wf::option_wrapper_t<bool> move_enable_snap_off{"move/enable_snap_off"};
    wf::option_wrapper_t<int> move_snap_off_threshold{"move/snap_off_threshold"};
    wf::option_wrapper_t<bool> move_join_views{"move/join_views"};

    wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> drag_helper;

    wf::option_wrapper_t<wf::config::compound_list_t<wf::activatorbinding_t>>
    workspace_bindings{"expo/workspace_bindings"};

    std::vector<wf::activator_callback> keyboard_select_cbs;
    std::vector<wf::option_sptr_t<wf::activatorbinding_t>> keyboard_select_options;

    struct
    {
        bool active = false;
        bool button_pressed = false;
        bool zoom_in = false;
    } state;

    wf::point_t target_ws, initial_ws;
    std::unique_ptr<wf::workspace_wall_t> wall;

    wf::key_repeat_t key_repeat;
    uint32_t key_pressed = 0;

    /* fade animations for each workspace */
    std::vector<std::vector<wf::animation::simple_animation_t>> ws_fade;

  public:
    void setup_workspace_bindings_from_config()
    {
        for (const auto& [workspace, binding] : workspace_bindings.value())
        {
            int workspace_index = atoi(workspace.c_str());
            auto wsize = output->workspace->get_workspace_grid_size();
            if ((workspace_index > (wsize.width * wsize.height)) ||
                (workspace_index < 1))
            {
                continue;
            }

            wf::point_t target = convert_workspace_index_to_coords(workspace_index);

            keyboard_select_options.push_back(wf::create_option(binding));
            keyboard_select_cbs.push_back([=] (auto)
            {
                if (!state.active)
                {
                    return false;
                } else
                {
                    if (!zoom_animation.running() || state.zoom_in)
                    {
                        if (target_ws != target)
                        {
                            shade_workspace(target_ws, true);
                            target_ws = target;
                            shade_workspace(target_ws, false);
                        }

                        deactivate();
                    }
                }

                return true;
            });
        }
    }

    void init() override
    {
        grab_interface->name = "expo";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;

        setup_workspace_bindings_from_config();
        wall = std::make_unique<wf::workspace_wall_t>(this->output);
        wall->connect_signal("frame", &on_frame);

        output->add_activator(toggle_binding, &toggle_cb);
        grab_interface->callbacks.pointer.button =
            [=] (uint32_t button, uint32_t state)
        {
            if (button != BTN_LEFT)
            {
                return;
            }

            auto gc = output->get_cursor_position();
            handle_input_press(gc.x, gc.y, state);
        };
        grab_interface->callbacks.pointer.motion = [=] (int32_t x, int32_t y)
        {
            handle_input_move({x, y});
        };

        grab_interface->callbacks.keyboard.key = [=] (uint32_t key, uint32_t state)
        {
            if (state == WLR_KEY_PRESSED)
            {
                if (should_handle_key())
                {
                    handle_key_pressed(key);
                }
            } else
            {
                if (key == key_pressed)
                {
                    key_repeat.disconnect();
                    key_pressed = 0;
                }
            }
        };

        grab_interface->callbacks.touch.down =
            [=] (int32_t id, wl_fixed_t sx, wl_fixed_t sy)
        {
            if (id > 0)
            {
                return;
            }

            handle_input_press(sx, sy, WLR_BUTTON_PRESSED);
        };

        grab_interface->callbacks.touch.up = [=] (int32_t id)
        {
            if (id > 0)
            {
                return;
            }

            handle_input_press(0, 0, WLR_BUTTON_RELEASED);
        };

        grab_interface->callbacks.touch.motion =
            [=] (int32_t id, int32_t sx, int32_t sy)
        {
            if (id > 0) // we handle just the first finger
            {
                return;
            }

            handle_input_move({sx, sy});
        };

        grab_interface->callbacks.cancel = [=] ()
        {
            finalize_and_exit();
        };

        drag_helper->connect_signal("focus-output", &on_drag_output_focus);
        drag_helper->connect_signal("snap-off", &on_drag_snap_off);
        drag_helper->connect_signal("done", &on_drag_done);

        resize_ws_fade();
        output->connect_signal("workspace-grid-changed", &on_workspace_grid_changed);
    }

    bool can_handle_drag()
    {
        return output->is_plugin_active(grab_interface->name);
    }

    wf::signal_connection_t on_drag_output_focus = [=] (auto data)
    {
        auto ev = static_cast<wf::move_drag::drag_focus_output_signal*>(data);
        if ((ev->focus_output == output) && can_handle_drag())
        {
            state.button_pressed = true;
            auto [vw, vh] = output->workspace->get_workspace_grid_size();
            drag_helper->set_scale(std::max(vw, vh));
        }
    };

    wf::signal_connection_t on_drag_snap_off = [=] (auto data)
    {
        auto ev = static_cast<wf::move_drag::snap_off_signal*>(data);
        if ((ev->focus_output == output) && can_handle_drag())
        {
            wf::move_drag::adjust_view_on_snap_off(drag_helper->view);
        }
    };

    wf::signal_connection_t on_drag_done = [=] (auto data)
    {
        auto ev = static_cast<wf::move_drag::drag_done_signal*>(data);
        if ((ev->focused_output == output) && can_handle_drag())
        {
            bool same_output = ev->main_view->get_output() == output;

            auto offset = wf::origin(output->get_layout_geometry());
            auto local  = input_coordinates_to_output_local_coordinates(
                ev->grab_position + -offset);

            for (auto& v :
                 wf::move_drag::get_target_views(ev->main_view, ev->join_views))
            {
                translate_wobbly(v, local - (ev->grab_position - offset));
            }

            ev->grab_position = local + offset;
            wf::move_drag::adjust_view_on_output(ev);

            if (same_output && (move_started_ws != offscreen_point))
            {
                wf::view_change_workspace_signal data;
                data.view = ev->main_view;
                data.from = move_started_ws;
                data.to   = target_ws;
                output->emit_signal("view-change-workspace", &data);
            }

            move_started_ws = offscreen_point;
        }

        this->state.button_pressed = false;
    };

    bool activate()
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        grab_interface->grab();

        state.active = true;
        state.button_pressed = false;
        start_zoom(true);

        auto cws = output->workspace->get_current_workspace();
        initial_ws = target_ws = cws;

        for (size_t i = 0; i < keyboard_select_cbs.size(); i++)
        {
            output->add_activator(keyboard_select_options[i],
                &keyboard_select_cbs[i]);
        }

        highlight_active_workspace();

        return true;
    }

    void start_zoom(bool zoom_in)
    {
        wall->set_background_color(background_color);
        wall->set_gap_size(this->delimiter_offset);
        if (zoom_in)
        {
            zoom_animation.set_start(wall->get_workspace_rectangle(
                output->workspace->get_current_workspace()));

            /* Make sure workspaces are centered */
            auto wsize = output->workspace->get_workspace_grid_size();
            auto size  = output->get_screen_size();
            const int maxdim = std::max(wsize.width, wsize.height);
            const int gap    = this->delimiter_offset;

            const int fullw = (gap + size.width) * maxdim + gap;
            const int fullh = (gap + size.height) * maxdim + gap;

            auto rectangle = wall->get_wall_rectangle();
            rectangle.x    -= (fullw - rectangle.width) / 2;
            rectangle.y    -= (fullh - rectangle.height) / 2;
            rectangle.width = fullw;
            rectangle.height = fullh;
            zoom_animation.set_end(rectangle);
        } else
        {
            zoom_animation.set_start(zoom_animation);
            zoom_animation.set_end(
                wall->get_workspace_rectangle(target_ws));
        }

        state.zoom_in = zoom_in;
        zoom_animation.start();
        wall->set_viewport(zoom_animation);
        wall->start_output_renderer();
        output->render->schedule_redraw();
    }

    void deactivate()
    {
        start_zoom(false);
        output->workspace->set_workspace(target_ws);
        for (size_t i = 0; i < keyboard_select_cbs.size(); i++)
        {
            output->rem_binding(&keyboard_select_cbs[i]);
        }
    }

    wf::geometry_t get_grid_geometry()
    {
        auto wsize  = output->workspace->get_workspace_grid_size();
        auto full_g = output->get_layout_geometry();

        wf::geometry_t grid;
        grid.x     = grid.y = 0;
        grid.width = full_g.width * wsize.width;
        grid.height = full_g.height * wsize.height;

        return grid;
    }

    wf::point_t input_grab_origin;
    void handle_input_press(int32_t x, int32_t y, uint32_t state)
    {
        if (zoom_animation.running())
        {
            return;
        }

        if ((state == WLR_BUTTON_RELEASED) && !this->drag_helper->view)
        {
            this->state.button_pressed = false;
            deactivate();
        } else if (state == WLR_BUTTON_RELEASED)
        {
            this->state.button_pressed = false;
            this->drag_helper->handle_input_released();
        } else
        {
            this->state.button_pressed = true;

            input_grab_origin = {x, y};
            update_target_workspace(x, y);
        }
    }

    void start_moving(wayfire_view view, wf::point_t grab)
    {
        auto workspace_impl =
            output->workspace->get_workspace_implementation();
        if (!workspace_impl->view_movable(view))
        {
            return;
        }

        auto ws_coords = input_coordinates_to_output_local_coordinates(grab);
        auto bbox = view->get_bounding_box("wobbly");

        view->damage();
        // Make sure that the view is in output-local coordinates!
        translate_wobbly(view, grab - ws_coords);

        auto [vw, vh] = output->workspace->get_workspace_grid_size();
        wf::move_drag::drag_options_t opts;
        opts.initial_scale   = std::max(vw, vh);
        opts.enable_snap_off = move_enable_snap_off &&
            (view->fullscreen || view->tiled_edges);
        opts.snap_off_threshold = move_snap_off_threshold;
        opts.join_views = move_join_views;

        auto output_offset = wf::origin(output->get_layout_geometry());
        drag_helper->start_drag(view, grab + output_offset,
            wf::move_drag::find_relative_grab(bbox, ws_coords), opts);
        move_started_ws = target_ws;
    }

    const wf::point_t offscreen_point = {-10, -10};
    void handle_input_move(wf::point_t to)
    {
        if (!state.button_pressed)
        {
            return;
        }

        auto output_offset = wf::origin(output->get_layout_geometry());
        if (drag_helper->view)
        {
            drag_helper->handle_motion(to + output_offset);
        }

        if (abs(to - input_grab_origin) < 5)
        {
            /* Ignore small movements */
            return;
        }

        bool first_click = (input_grab_origin != offscreen_point);
        if (!zoom_animation.running() && first_click)
        {
            auto view = find_view_at_coordinates(input_grab_origin.x,
                input_grab_origin.y);
            if (view)
            {
                start_moving(view, input_grab_origin);
                drag_helper->handle_motion(to + output_offset);
            }
        }

        /* As input coordinates are always positive, this will ensure that any
         * subsequent motion events while grabbed are allowed */
        input_grab_origin = offscreen_point;
        update_target_workspace(to.x, to.y);
    }

    /**
     * Helper to determine if keyboard presses should be handled
     */
    bool should_handle_key()
    {
        return ((!zoom_animation.running() || state.zoom_in) &&
            keyboard_interaction && !state.button_pressed);
    }

    void handle_key_pressed(uint32_t key)
    {
        wf::point_t old_target = target_ws;

        switch (key)
        {
          case KEY_ENTER:
            deactivate();
            return;

          case KEY_ESC:
            target_ws = initial_ws;
            shade_workspace(old_target, true);
            shade_workspace(target_ws, false);
            deactivate();
            return;

          case KEY_UP:
          case KEY_K:
            target_ws.y -= 1;
            break;

          case KEY_DOWN:
          case KEY_J:
            target_ws.y += 1;
            break;

          case KEY_RIGHT:
          case KEY_L:
            target_ws.x += 1;
            break;

          case KEY_LEFT:
          case KEY_H:
            target_ws.x -= 1;
            break;

          default:
            return;
        }

        /* this part is only reached if one of the arrow keys is pressed */
        if (key != key_pressed)
        {
            // update key repeat callbacks
            // (note: this will disconnect any previous callback)
            key_repeat.set_callback(key, [this] (uint32_t key)
            {
                if (!should_handle_key())
                {
                    // disconnect if key events should no longer be handled
                    key_pressed = 0;
                    return false;
                }

                handle_key_pressed(key);
                return true; // repeat
            });

            key_pressed = key;
        }

        // ensure that the new target is valid (use wrap-around)
        auto dim = output->workspace->get_workspace_grid_size();
        target_ws.x = (target_ws.x + dim.width) % dim.width;
        target_ws.y = (target_ws.y + dim.height) % dim.height;

        shade_workspace(old_target, true);
        shade_workspace(target_ws, false);
    }

    /**
     * shade all but the selected workspace instantly (without animation)
     */
    void highlight_active_workspace()
    {
        auto dim = output->workspace->get_workspace_grid_size();
        for (int x = 0; x < dim.width; x++)
        {
            for (int y = 0; y < dim.height; y++)
            {
                if ((x == target_ws.x) && (y == target_ws.y))
                {
                    wall->get_ws_color({x, y}) = glm::vec4(1.0f);
                } else
                {
                    wall->get_ws_color({x, y}) = glm::vec4(
                        (float)inactive_brightness,
                        (float)inactive_brightness,
                        (float)inactive_brightness,
                        1.0f);
                }
            }
        }
    }

    /**
     * start an animation for shading the given workspace
     */
    void shade_workspace(const wf::point_t& ws, bool shaded)
    {
        double target = shaded ? inactive_brightness : 1.0;
        auto& anim    = ws_fade.at(ws.x).at(ws.y);

        if (anim.running())
        {
            anim.animate(target);
        } else
        {
            anim.animate(shaded ? 1.0 : inactive_brightness, target);
        }

        output->render->schedule_redraw();
    }

    wf::point_t move_started_ws = offscreen_point;

    /**
     * Find the coordinate of the given point from output-local coordinates
     * to coordinates relative to the first workspace (i.e (0,0))
     */
    void input_coordinates_to_global_coordinates(int & sx, int & sy)
    {
        auto og = output->get_layout_geometry();

        auto wsize = output->workspace->get_workspace_grid_size();
        float max  = std::max(wsize.width, wsize.height);

        float grid_start_x = og.width * (max - wsize.width) / float(max) / 2;
        float grid_start_y = og.height * (max - wsize.height) / float(max) / 2;

        sx -= grid_start_x;
        sy -= grid_start_y;

        sx *= max;
        sy *= max;
    }

    /**
     * Find the coordinate of the given point from output-local coordinates
     * to output-workspace-local coordinates
     */
    wf::point_t input_coordinates_to_output_local_coordinates(wf::point_t ip)
    {
        input_coordinates_to_global_coordinates(ip.x, ip.y);

        auto cws = output->workspace->get_current_workspace();
        auto og  = output->get_relative_geometry();

        /* Translate coordinates into output-local coordinate system,
         * relative to the current workspace */
        return {
            ip.x - cws.x * og.width,
            ip.y - cws.y * og.height,
        };
    }

    /**
     * If the view is sticky, return the pos relative to the current workspace.
     * Otherwise, it stays the same.
     */
    wf::point_t view_local_coordinates(wayfire_view view, wf::point_t pos)
    {
        auto ssize = output->get_screen_size();
        if (view->sticky)
        {
            return {
                (pos.x % ssize.width + ssize.width) % ssize.width,
                (pos.y % ssize.height + ssize.height) % ssize.height
            };
        } else
        {
            return pos;
        }
    }

    wayfire_view find_view_at_coordinates(int gx, int gy)
    {
        auto local = input_coordinates_to_output_local_coordinates({gx, gy});
        /* TODO: adjust to delimiter offset */
        for (auto& view : output->workspace->get_views_in_layer(wf::WM_LAYERS))
        {
            if (!view->is_mapped() || !view->is_visible())
            {
                continue;
            }

            auto view_local = view_local_coordinates(view, local);
            wlr_box box     = {view_local.x, view_local.y, 1, 1};
            for (auto& v : view->enumerate_views())
            {
                if (v->intersects_region(box))
                {
                    return v;
                }
            }
        }

        return nullptr;
    }

    void update_target_workspace(int x, int y)
    {
        auto og = output->get_layout_geometry();

        input_coordinates_to_global_coordinates(x, y);

        auto grid = get_grid_geometry();
        if (!(grid & wf::point_t{x, y}))
        {
            return;
        }

        int tmpx = x / og.width;
        int tmpy = y / og.height;
        if ((tmpx != target_ws.x) || (tmpy != target_ws.y))
        {
            shade_workspace(target_ws, true);
            target_ws = {tmpx, tmpy};
            shade_workspace(target_ws, false);
        }
    }

    wf::signal_connection_t on_frame = {[=] (wf::signal_data_t*)
        {
            if (zoom_animation.running())
            {
                output->render->schedule_redraw();
                wall->set_viewport(zoom_animation);
            } else if (!state.zoom_in)
            {
                finalize_and_exit();
                return;
            }

            auto size = this->output->workspace->get_workspace_grid_size();
            for (int x = 0; x < size.width; x++)
            {
                for (int y = 0; y < size.height; y++)
                {
                    auto& anim = ws_fade.at(x).at(y);
                    if (anim.running())
                    {
                        wall->get_ws_color({x, y}) = glm::vec4(
                            anim, anim, anim, 1.0f);
                        output->render->schedule_redraw();
                    }
                }
            }
        }
    };

    void resize_ws_fade()
    {
        auto size = this->output->workspace->get_workspace_grid_size();
        ws_fade.resize(size.width);
        for (auto& v : ws_fade)
        {
            size_t h = size.height;
            if (v.size() > h)
            {
                v.resize(h);
            } else
            {
                while (v.size() < h)
                {
                    v.emplace_back(transition_length);
                }
            }
        }
    }

    wf::signal_connection_t on_workspace_grid_changed = [=] (auto)
    {
        resize_ws_fade();

        // check that the target and initial workspaces are still in the grid
        auto size = this->output->workspace->get_workspace_grid_size();
        initial_ws.x = std::min(initial_ws.x, size.width - 1);
        initial_ws.y = std::min(initial_ws.y, size.height - 1);

        if ((target_ws.x >= size.width) || (target_ws.y >= size.height))
        {
            target_ws.x = std::min(target_ws.x, size.width - 1);
            target_ws.y = std::min(target_ws.y, size.height - 1);
            highlight_active_workspace();
        }
    };

    void finalize_and_exit()
    {
        state.active = false;
        if (drag_helper->view)
        {
            drag_helper->handle_input_released();
        }

        output->deactivate_plugin(grab_interface);
        grab_interface->ungrab();
        wall->stop_output_renderer(true);
        key_repeat.disconnect();
        key_pressed = 0;
    }

    void fini() override
    {
        if (state.active)
        {
            finalize_and_exit();
        }

        output->rem_binding(&toggle_cb);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_expo);
