/**
 * Original code by: Scott Moreau, Daniel Kondor
 */
#include <map>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/vswitch.hpp>
#include <wayfire/touch/touch.hpp>
#include <wayfire/plugins/scale-signal.hpp>
#include <wayfire/plugins/scale-transform.hpp>
#include <wayfire/plugins/wobbly/wobbly-signal.hpp>

#include <wayfire/plugins/common/move-drag-interface.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>

#include <linux/input-event-codes.h>

#include "scale-title-overlay.hpp"

using namespace wf::animation;

class scale_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t scale_x{*this};
    timed_transition_t scale_y{*this};
    timed_transition_t translation_x{*this};
    timed_transition_t translation_y{*this};
};

struct wf_scale_animation_attribs
{
    wf::option_wrapper_t<int> duration{"scale/duration"};
    scale_animation_t scale_animation{duration};
};

struct view_scale_data
{
    int row, col;
    wf::scale_transformer_t *transformer = nullptr;
    wf::animation::simple_animation_t fade_animation;
    wf_scale_animation_attribs animation;
    enum class view_visibility_t
    {
        VISIBLE, /*  view is shown in position determined by layout_slots() */
        HIDING, /* view is in the process of hiding (due to filters)      */
        HIDDEN, /* view is hidden by a filter (with set_visible(false))   */
    };

    view_visibility_t visibility = view_visibility_t::VISIBLE;
};

class wayfire_scale : public wf::plugin_interface_t
{
    /* helper class for optionally showing title overlays */
    scale_show_title_t show_title;
    std::vector<int> current_row_sizes;
    wf::point_t initial_workspace;
    bool active, hook_set;
    /* View that was active before scale began. */
    wayfire_view initial_focus_view;
    /* View that has active focus. */
    wayfire_view current_focus_view;
    // View over which the last input press happened, might become dangling
    wayfire_view last_selected_view;
    std::map<wayfire_view, view_scale_data> scale_data;
    wf::option_wrapper_t<int> spacing{"scale/spacing"};
    /* If interact is true, no grab is acquired and input events are sent
     * to the scaled surfaces. If it is false, the hard coded bindings
     * are as follows:
     * KEY_ENTER:
     * - Ends scale, switching to the workspace of the focused view
     * KEY_ESC:
     * - Ends scale, switching to the workspace where scale was started,
     *   and focuses the initially active view
     * KEY_UP:
     * KEY_DOWN:
     * KEY_LEFT:
     * KEY_RIGHT:
     * - When scale is active, change focus of the views
     *
     * BTN_LEFT:
     * - Ends scale, switching to the workspace of the surface clicked
     * BTN_MIDDLE:
     * - If middle_click_close is true, closes the view clicked
     */
    wf::option_wrapper_t<bool> interact{"scale/interact"};
    wf::option_wrapper_t<bool> middle_click_close{"scale/middle_click_close"};
    wf::option_wrapper_t<double> inactive_alpha{"scale/inactive_alpha"};
    wf::option_wrapper_t<bool> allow_scale_zoom{"scale/allow_zoom"};

    /* maximum scale -- 1.0 means we will not "zoom in" on a view */
    const double max_scale_factor = 1.0;
    /* maximum scale for child views (relative to their parents)
     * zero means unconstrained, 1.0 means child cannot be scaled
     * "larger" than the parent */
    const double max_scale_child = 1.0;

    /* true if the currently running scale should include views from
     * all workspaces */
    bool all_workspaces;
    std::unique_ptr<wf::vswitch::control_bindings_t> workspace_bindings;

    wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> drag_helper;

  public:
    void init() override
    {
        grab_interface->name = "scale";
        grab_interface->capabilities =
            wf::CAPABILITY_MANAGE_DESKTOP | wf::CAPABILITY_GRAB_INPUT;
        active = hook_set = false;

        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"scale/toggle"},
            &toggle_cb);
        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"scale/toggle_all"},
            &toggle_all_cb);
        output->connect_signal("scale-update", &update_cb);

        grab_interface->callbacks.keyboard.key = [=] (uint32_t key, uint32_t state)
        {
            process_key(key, state);
        };

        grab_interface->callbacks.cancel = [=] ()
        {
            finalize();
        };

        grab_interface->callbacks.pointer.motion = [=] (int32_t x, int32_t y)
        {
            auto offset = wf::origin(output->get_layout_geometry());
            process_motion(offset + wf::point_t{x, y});
        };

        interact.set_callback(interact_option_changed);
        allow_scale_zoom.set_callback(allow_scale_zoom_option_changed);

        setup_workspace_switching();

        drag_helper->connect_signal("focus-output", &on_drag_output_focus);
        drag_helper->connect_signal("done", &on_drag_done);
        drag_helper->connect_signal("snap-off", &on_drag_snap_off);

        show_title.init(output);
    }

    void setup_workspace_switching()
    {
        workspace_bindings =
            std::make_unique<wf::vswitch::control_bindings_t>(output);
        workspace_bindings->setup([&] (wf::point_t delta, wayfire_view view)
        {
            if (!output->is_plugin_active(grab_interface->name))
            {
                return false;
            }

            if (delta == wf::point_t{0, 0})
            {
                // Consume input event
                return true;
            }

            auto ws = output->workspace->get_current_workspace() + delta;

            // vswitch picks the top view, we want the focused one
            std::vector<wayfire_view> fixed_views;
            if (view && !all_workspaces)
            {
                fixed_views.push_back(current_focus_view);
            }

            output->workspace->request_workspace(ws, fixed_views);

            return true;
        });
    }

    /* Add a transformer that will be used to scale the view */
    bool add_transformer(wayfire_view view)
    {
        if (view->get_transformer(wf::scale_transformer_t::transformer_name()))
        {
            return false;
        }

        wf::scale_transformer_t *tr = new wf::scale_transformer_t(view);
        scale_data[view].transformer = tr;
        view->add_transformer(std::unique_ptr<wf::scale_transformer_t>(tr),
            wf::scale_transformer_t::transformer_name());
        /* Transformers are added only once when scale is activated so
         * this is a good place to connect the geometry-changed handler */
        view->connect_signal("geometry-changed", &view_geometry_changed);

        set_tiled_wobbly(view, true);

        /* signal that a transformer was added to this view */
        scale_transformer_added_signal data;
        data.transformer = tr;
        output->emit_signal("scale-transformer-added", &data);

        return true;
    }

    /* Remove the scale transformer from the view */
    void pop_transformer(wayfire_view view)
    {
        view->pop_transformer(wf::scale_transformer_t::transformer_name());
        set_tiled_wobbly(view, false);
    }

    /* Remove scale transformers from all views */
    void remove_transformers()
    {
        for (auto& e : scale_data)
        {
            for (auto& toplevel : e.first->enumerate_views(false))
            {
                pop_transformer(toplevel);
            }

            if (e.second.visibility == view_scale_data::view_visibility_t::HIDDEN)
            {
                e.first->set_visible(true);
            }

            e.second.visibility = view_scale_data::view_visibility_t::VISIBLE;
        }
    }

    /* Check whether views exist on other workspaces */
    bool all_same_as_current_workspace_views()
    {
        return get_all_workspace_views().size() ==
               get_current_workspace_views().size();
    }

    /* Activate scale, switch activator modes and deactivate */
    bool handle_toggle(bool want_all_workspaces)
    {
        if (active && (all_same_as_current_workspace_views() ||
                       (want_all_workspaces == this->all_workspaces)))
        {
            deactivate();

            return true;
        }

        this->all_workspaces = want_all_workspaces;
        if (active)
        {
            switch_scale_modes();

            return true;
        } else
        {
            return activate();
        }
    }

    /* Activate scale for views on the current workspace */
    wf::activator_callback toggle_cb = [=] (auto)
    {
        if (handle_toggle(false))
        {
            output->render->schedule_redraw();

            return true;
        }

        return false;
    };

    /* Activate scale for views on all workspaces */
    wf::activator_callback toggle_all_cb = [=] (auto)
    {
        if (handle_toggle(true))
        {
            output->render->schedule_redraw();

            return true;
        }

        return false;
    };

    wf::signal_connection_t update_cb{[=] (wf::signal_data_t*)
        {
            if (active)
            {
                layout_slots(get_views());
                output->render->schedule_redraw();
            }
        }
    };

    /* Connect button signal */
    void connect_button_signal()
    {
        disconnect_button_signal();
        wf::get_core().connect_signal("pointer_button_post", &on_button_event);
        wf::get_core().connect_signal("touch_down_post", &on_touch_down_event);
        // connect to the signal before touching up, so that the touch point
        // is still active.
        wf::get_core().connect_signal("touch_up", &on_touch_up_event);
    }

    /* Disconnect button signal */
    void disconnect_button_signal()
    {
        on_button_event.disconnect();
        on_touch_down_event.disconnect();
        on_touch_up_event.disconnect();
    }

    /* For button processing without grabbing */
    wf::signal_connection_t on_button_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_pointer_button_event>*>(data);

        process_input(ev->event->button, ev->event->state,
            wf::get_core().get_cursor_position());
    };

    wf::signal_connection_t on_touch_down_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_touch_down_event>*>(data);
        if (ev->event->touch_id == 0)
        {
            process_input(BTN_LEFT, WLR_BUTTON_PRESSED,
                wf::get_core().get_touch_position(0));
        }
    };

    wf::signal_connection_t on_touch_up_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_touch_up_event>*>(data);
        if (ev->event->touch_id == 0)
        {
            process_input(BTN_LEFT, WLR_BUTTON_RELEASED,
                wf::get_core().get_touch_position(0));
        }
    };

    /** Return the topmost parent */
    wayfire_view get_top_parent(wayfire_view view)
    {
        while (view && view->parent)
        {
            view = view->parent;
        }

        return view;
    }

    /* Fade all views' alpha to inactive alpha except the
     * view argument */
    void fade_out_all_except(wayfire_view view)
    {
        for (auto& e : scale_data)
        {
            auto v = e.first;
            if (get_top_parent(v) == get_top_parent(view))
            {
                continue;
            }

            if (e.second.visibility != view_scale_data::view_visibility_t::VISIBLE)
            {
                continue;
            }

            fade_out(v);
        }
    }

    /* Fade in view alpha */
    void fade_in(wayfire_view view)
    {
        if (!view || !scale_data.count(view))
        {
            return;
        }

        set_hook();
        auto alpha = scale_data[view].transformer->alpha;
        scale_data[view].fade_animation.animate(alpha, 1);
        if (view->children.size())
        {
            fade_in(view->children.front());
        }
    }

    /* Fade out view alpha */
    void fade_out(wayfire_view view)
    {
        if (!view)
        {
            return;
        }

        set_hook();
        for (auto v : view->enumerate_views(false))
        {
            // Could happen if we have a never-mapped child view
            if (!scale_data.count(v))
            {
                continue;
            }

            auto alpha = scale_data[v].transformer->alpha;
            scale_data[v].fade_animation.animate(alpha, (double)inactive_alpha);
        }
    }

    /* Switch to the workspace for the untransformed view geometry */
    void select_view(wayfire_view view)
    {
        if (!view)
        {
            return;
        }

        auto ws = get_view_main_workspace(view);
        output->workspace->request_workspace(ws);
    }

    /* Updates current and initial view focus variables accordingly */
    void check_focus_view(wayfire_view view)
    {
        if (view == current_focus_view)
        {
            current_focus_view = output->get_active_view();
        }

        if (view == initial_focus_view)
        {
            initial_focus_view = nullptr;
        }
    }

    /* Remove transformer from view and remove view from the scale_data map */
    void remove_view(wayfire_view view)
    {
        if (!view)
        {
            return;
        }

        for (auto v : view->enumerate_views(false))
        {
            check_focus_view(v);
            pop_transformer(v);
            scale_data.erase(v);
        }
    }

    /* Process button event */
    void process_input(uint32_t button, uint32_t state,
        wf::pointf_t input_position)
    {
        if (!active)
        {
            return;
        }

        if (state == WLR_BUTTON_PRESSED)
        {
            auto view = wf::get_core().get_view_at(input_position);
            if (view && should_scale_view(view))
            {
                // Mark the view as the target of the next input release operation
                last_selected_view = view;
            } else
            {
                last_selected_view = nullptr;
            }

            return;
        }

        if (drag_helper->view)
        {
            drag_helper->handle_input_released();
        }

        auto view = wf::get_core().get_view_at(input_position);
        if (!view || (last_selected_view != view))
        {
            last_selected_view = nullptr;
            // Operation was cancelled, for ex. dragged outside of the view
            return;
        }

        // Reset last_selected_view, because it is no longer held
        last_selected_view = nullptr;
        switch (button)
        {
          case BTN_LEFT:
            // Focus the view under the mouse
            current_focus_view = view;
            fade_out_all_except(view);
            fade_in(get_top_parent(view));
            if (!interact)
            {
                // End scale
                initial_focus_view = nullptr;
                deactivate();
                select_view(view);
            }

            output->focus_view(view, false);

            break;

          case BTN_MIDDLE:
            // Check kill the view
            if (middle_click_close)
            {
                view->close();
            }

            break;

          default:
            break;
        }
    }

    void process_motion(wf::point_t to)
    {
        if (!drag_helper->view && last_selected_view)
        {
            wf::move_drag::drag_options_t opts;
            opts.join_views = true;
            opts.enable_snap_off    = true;
            opts.snap_off_threshold = 200;

            drag_helper->start_drag(last_selected_view, to, opts);
        } else if (drag_helper->view)
        {
            drag_helper->handle_motion(to);
            if (last_selected_view)
            {
                const double threshold = 20.0;
                if (drag_helper->distance_to_grab_origin(to) > threshold)
                {
                    last_selected_view = nullptr;
                }
            }
        }
    }

    /* Get the workspace for the center point of the untransformed view geometry */
    wf::point_t get_view_main_workspace(wayfire_view view)
    {
        while (view->parent)
        {
            view = view->parent;
        }

        auto ws = output->workspace->get_current_workspace();
        auto og = output->get_layout_geometry();
        auto vg = scale_data.count(view) > 0 ?
            view->get_bounding_box(scale_data[view].transformer) :
            view->get_bounding_box();
        auto center = wf::point_t{vg.x + vg.width / 2, vg.y + vg.height / 2};

        return wf::point_t{
            ws.x + (int)std::floor((double)center.x / og.width),
            ws.y + (int)std::floor((double)center.y / og.height)};
    }

    /* Given row and column, return a view at this position in the scale grid,
     * or the first scaled view if none is found */
    wayfire_view find_view_in_grid(int row, int col)
    {
        for (auto& view : scale_data)
        {
            if ((view.first->parent == nullptr) &&
                (view.second.visibility ==
                 view_scale_data::view_visibility_t::VISIBLE) &&
                ((view.second.row == row) &&
                 (view.second.col == col)))
            {
                return view.first;
            }
        }

        return get_views().front();
    }

    /* Process key event */
    void process_key(uint32_t key, uint32_t state)
    {
        auto view = output->get_active_view();
        if (!view)
        {
            view = current_focus_view;
            if (view)
            {
                fade_out_all_except(view);
                fade_in(view);
                output->focus_view(view, true);

                return;
            }
        } else if (!scale_data.count(view))
        {
            return;
        }

        int cur_row  = view ? scale_data[view].row : 0;
        int cur_col  = view ? scale_data[view].col : 0;
        int next_row = cur_row;
        int next_col = cur_col;

        if ((state != WLR_KEY_PRESSED) ||
            wf::get_core().get_keyboard_modifiers())
        {
            return;
        }

        switch (key)
        {
          case KEY_UP:
            next_row--;
            break;

          case KEY_DOWN:
            next_row++;
            break;

          case KEY_LEFT:
            next_col--;
            break;

          case KEY_RIGHT:
            next_col++;
            break;

          case KEY_ENTER:
            deactivate();
            select_view(current_focus_view);
            output->focus_view(current_focus_view, true);

            return;

          case KEY_ESC:
            deactivate();
            output->workspace->request_workspace(initial_workspace);
            output->focus_view(initial_focus_view, true);
            initial_focus_view = nullptr;

            return;

          default:
            return;
        }

        if (!view)
        {
            return;
        }

        if (!current_row_sizes.empty())
        {
            next_row = (next_row + current_row_sizes.size()) %
                current_row_sizes.size();

            if (cur_row != next_row)
            {
                /* when moving to and from the last row, the number of columns
                 * may be different, so this bit figures out which view we
                 * should switch focus to */
                float p = 1.0 * cur_col / current_row_sizes[cur_row];
                next_col = p * current_row_sizes[next_row];
            } else
            {
                next_col = (next_col + current_row_sizes[cur_row]) %
                    current_row_sizes[cur_row];
            }
        } else
        {
            next_row = cur_row;
            next_col = cur_col;
        }

        view = find_view_in_grid(next_row, next_col);
        if (view && (current_focus_view != view))
        {
            // view_focused handler will update the view state
            output->focus_view(view, false);
        }
    }

    /* Assign the transformer values to the view transformers */
    void transform_views()
    {
        for (auto& e : scale_data)
        {
            auto view = e.first;
            auto& view_data = e.second;
            if (!view || !view_data.transformer)
            {
                continue;
            }

            bool needs_damage = false;

            if (view_data.fade_animation.running() ||
                view_data.animation.scale_animation.running())
            {
                view->damage();
                view_data.transformer->scale_x =
                    view_data.animation.scale_animation.scale_x;
                view_data.transformer->scale_y =
                    view_data.animation.scale_animation.scale_y;
                view_data.transformer->translation_x =
                    view_data.animation.scale_animation.translation_x;
                view_data.transformer->translation_y =
                    view_data.animation.scale_animation.translation_y;
                view_data.transformer->alpha = view_data.fade_animation;
                needs_damage = true;

                if ((view_data.visibility ==
                     view_scale_data::view_visibility_t::HIDING) &&
                    !view_data.fade_animation.running())
                {
                    view_data.visibility =
                        view_scale_data::view_visibility_t::HIDDEN;
                    view->set_visible(false);
                }
            }

            view_data.transformer->call_pre_hooks(needs_damage);
        }
    }

    /* Returns a list of views for all workspaces */
    std::vector<wayfire_view> get_all_workspace_views()
    {
        std::vector<wayfire_view> views;

        for (auto& view :
             output->workspace->get_views_in_layer(wf::LAYER_WORKSPACE))
        {
            if ((view->role != wf::VIEW_ROLE_TOPLEVEL) || !view->is_mapped())
            {
                continue;
            }

            views.push_back(view);
        }

        return views;
    }

    /* Returns a list of views for the current workspace */
    std::vector<wayfire_view> get_current_workspace_views()
    {
        std::vector<wayfire_view> views;

        for (auto& view :
             output->workspace->get_views_in_layer(wf::LAYER_WORKSPACE))
        {
            if ((view->role != wf::VIEW_ROLE_TOPLEVEL) || !view->is_mapped())
            {
                continue;
            }

            auto vg = view->get_wm_geometry();
            auto og = output->get_relative_geometry();
            wf::region_t wr{og};
            wf::point_t center{vg.x + vg.width / 2, vg.y + vg.height / 2};

            if (wr.contains_point(center))
            {
                views.push_back(view);
            }
        }

        return views;
    }

    /* Returns a list of views to be scaled */
    std::vector<wayfire_view> get_views()
    {
        std::vector<wayfire_view> views;

        if (all_workspaces)
        {
            views = get_all_workspace_views();
        } else
        {
            views = get_current_workspace_views();
        }

        return views;
    }

    /**
     * @return true if the view is to be scaled.
     */
    bool should_scale_view(wayfire_view view)
    {
        auto views = get_views();

        return std::find(
            views.begin(), views.end(), get_top_parent(view)) != views.end();
    }

    /* Convenience assignment function */
    void setup_view_transform(view_scale_data& view_data,
        double scale_x,
        double scale_y,
        double translation_x,
        double translation_y,
        double target_alpha)
    {
        view_data.animation.scale_animation.scale_x.set(
            view_data.transformer->scale_x, scale_x);
        view_data.animation.scale_animation.scale_y.set(
            view_data.transformer->scale_y, scale_y);
        view_data.animation.scale_animation.translation_x.set(
            view_data.transformer->translation_x, translation_x);
        view_data.animation.scale_animation.translation_y.set(
            view_data.transformer->translation_y, translation_y);
        view_data.animation.scale_animation.start();
        view_data.fade_animation = wf::animation::simple_animation_t(
            wf::option_wrapper_t<int>{"scale/duration"});
        view_data.fade_animation.animate(view_data.transformer->alpha,
            target_alpha);
    }

    static bool view_compare_x(const wayfire_view& a, const wayfire_view& b)
    {
        auto vg_a = a->get_wm_geometry();
        std::vector<int> a_coords = {vg_a.x, vg_a.width, vg_a.y, vg_a.height};
        auto vg_b = b->get_wm_geometry();
        std::vector<int> b_coords = {vg_b.x, vg_b.width, vg_b.y, vg_b.height};
        return a_coords < b_coords;
    }

    static bool view_compare_y(const wayfire_view& a, const wayfire_view& b)
    {
        auto vg_a = a->get_wm_geometry();
        std::vector<int> a_coords = {vg_a.y, vg_a.height, vg_a.x, vg_a.width};
        auto vg_b = b->get_wm_geometry();
        std::vector<int> b_coords = {vg_b.y, vg_b.height, vg_b.x, vg_b.width};
        return a_coords < b_coords;
    }

    std::vector<std::vector<wayfire_view>> view_sort(
        std::vector<wayfire_view>& views)
    {
        std::vector<std::vector<wayfire_view>> view_grid;
        std::sort(views.begin(), views.end(), view_compare_y);

        int rows = sqrt(views.size() + 1);
        int views_per_row = (int)std::ceil((double)views.size() / rows);
        size_t n = views.size();
        for (size_t i = 0; i < n; i += views_per_row)
        {
            size_t j = std::min(i + views_per_row, n);
            view_grid.emplace_back(views.begin() + i, views.begin() + j);
            std::sort(view_grid.back().begin(), view_grid.back().end(),
                view_compare_x);
        }

        return view_grid;
    }

    /* Filter the views to be arranged by layout_slots() */
    void filter_views(std::vector<wayfire_view>& views)
    {
        std::vector<wayfire_view> filtered_views;
        scale_filter_signal signal(views, filtered_views);
        output->emit_signal("scale-filter", &signal);

        /* update hidden views -- ensure that they and their children have a
         * transformer and are in scale_data */
        for (auto view : filtered_views)
        {
            for (auto v : view->enumerate_views(false))
            {
                add_transformer(v);
                auto& view_data = scale_data[v];
                if (view_data.visibility ==
                    view_scale_data::view_visibility_t::VISIBLE)
                {
                    view_data.visibility =
                        view_scale_data::view_visibility_t::HIDING;
                    setup_view_transform(view_data, 1, 1, 0, 0, 0);
                }

                if (v == current_focus_view)
                {
                    current_focus_view = nullptr;
                }
            }
        }

        if (!current_focus_view)
        {
            current_focus_view = views.empty() ? nullptr : views.front();
            output->focus_view(current_focus_view, true);
        }
    }

    /* Compute target scale layout geometry for all the view transformers
     * and start animating. Initial code borrowed from the compiz scale
     * plugin algorithm */
    void layout_slots(std::vector<wayfire_view> views)
    {
        if (!views.size())
        {
            if (!all_workspaces && active)
            {
                deactivate();
            }

            return;
        }

        filter_views(views);

        auto workarea = output->workspace->get_workarea();

        auto sorted_rows = view_sort(views);
        size_t cnt_rows  = sorted_rows.size();

        const double scaled_height = std::max((double)
            (workarea.height - (cnt_rows + 1) * spacing) / cnt_rows, 1.0);
        current_row_sizes.clear();

        for (size_t i = 0; i < cnt_rows; i++)
        {
            size_t cnt_cols = sorted_rows[i].size();
            current_row_sizes.push_back(cnt_cols);
            const double scaled_width = std::max((double)
                (workarea.width - (cnt_cols + 1) * spacing) / cnt_cols, 1.0);

            for (size_t j = 0; j < cnt_cols; j++)
            {
                double x = workarea.x + spacing + (spacing + scaled_width) * j;
                double y = workarea.y + spacing + (spacing + scaled_height) * i;

                auto view = sorted_rows[i][j];

                // Calculate current transformation of the view, in order to
                // ensure that new views in the view tree start directly at the
                // correct position
                double main_view_dx    = 0;
                double main_view_dy    = 0;
                double main_view_scale = 1.0;
                if (scale_data.count(view))
                {
                    main_view_dx    = scale_data[view].transformer->translation_x;
                    main_view_dy    = scale_data[view].transformer->translation_y;
                    main_view_scale = scale_data[view].transformer->scale_x;
                }

                // Calculate target alpha for this view and its children
                double target_alpha =
                    (view == current_focus_view) ? 1 : (double)inactive_alpha;

                // Helper function to calculate the desired scale for a view
                const auto& calculate_scale = [=] (wf::dimensions_t vg,
                                                   const wf::scale_transformer_t::
                                                   padding_t& pad)
                {
                    double w = std::max(1.0, scaled_width - pad.left - pad.right);
                    double h = std::max(1.0, scaled_height - pad.top - pad.bottom);

                    const double scale = std::min(w / vg.width, h / vg.height);
                    if (!allow_scale_zoom)
                    {
                        return std::min(scale, max_scale_factor);
                    }

                    return scale;
                };

                add_transformer(view);
                auto geom = view->transform_region(view->get_wm_geometry(),
                    scale_data[view].transformer);
                double view_scale = calculate_scale({geom.width, geom.height},
                    scale_data[view].transformer->get_scale_padding());
                for (auto& child : view->enumerate_views(false))
                {
                    // Ensure a transformer for the view, and make sure that
                    // new views in the view tree start off with the correct
                    // attributes set.
                    auto new_child   = add_transformer(child);
                    auto& child_data = scale_data[child];
                    if (new_child)
                    {
                        child_data.transformer->translation_x = main_view_dx;
                        child_data.transformer->translation_y = main_view_dy;
                        child_data.transformer->scale_x = main_view_scale;
                        child_data.transformer->scale_y = main_view_scale;
                    }

                    if (child_data.visibility ==
                        view_scale_data::view_visibility_t::HIDDEN)
                    {
                        child->set_visible(true);
                    }

                    child_data.visibility =
                        view_scale_data::view_visibility_t::VISIBLE;

                    child_data.row = i;
                    child_data.col = j;

                    if (!active)
                    {
                        // On exit, we just animate towards normal state
                        setup_view_transform(child_data, 1, 1, 0, 0, 1);
                        continue;
                    }

                    auto vg = child->transform_region(child->get_wm_geometry(),
                        child_data.transformer);
                    wf::pointf_t center = {vg.x + vg.width / 2.0,
                        vg.y + vg.height / 2.0};

                    // Take padding into account
                    auto pad     = child_data.transformer->get_scale_padding();
                    double scale = calculate_scale({vg.width, vg.height}, pad);
                    // Ensure child is not scaled more than parent
                    if (!allow_scale_zoom &&
                        (child != view) &&
                        (max_scale_child > 0.0))
                    {
                        scale = std::min(max_scale_child * view_scale, scale);
                    }

                    // Target geometry is centered around the center slot
                    const double dx = x + pad.left - center.x + scaled_width / 2.0;
                    const double dy = y + pad.top - center.y + scaled_height / 2.0;
                    setup_view_transform(child_data, scale, scale,
                        dx, dy, target_alpha);
                }
            }
        }

        set_hook();
        transform_views();
    }

    /* Handle interact option changed */
    wf::config::option_base_t::updated_callback_t interact_option_changed = [=] ()
    {
        if (!output->is_plugin_active(grab_interface->name))
        {
            return;
        }

        if (interact)
        {
            grab_interface->ungrab();
        } else
        {
            grab_interface->grab();
        }
    };

    /* Called when adding or removing a group of views to be scaled,
     * in this case between views on all workspaces and views on the
     * current workspace */
    void switch_scale_modes()
    {
        if (!output->is_plugin_active(grab_interface->name))
        {
            return;
        }

        if (all_workspaces)
        {
            layout_slots(get_views());

            return;
        }

        bool rearrange = false;
        for (auto& e : scale_data)
        {
            if (!should_scale_view(e.first))
            {
                setup_view_transform(e.second, 1, 1, 0, 0, 1);
                rearrange = true;
            }
        }

        if (rearrange)
        {
            layout_slots(get_views());
        }
    }

    /* Toggle between restricting maximum scale to 100% or allowing it
     * to become the greater. This is particularly noticeable when
     * scaling a single view or a view with child views. */
    wf::config::option_base_t::updated_callback_t allow_scale_zoom_option_changed =
        [=] ()
    {
        if (!output->is_plugin_active(grab_interface->name))
        {
            return;
        }

        layout_slots(get_views());
    };

    /* New view or view moved to output with scale active */
    wf::signal_connection_t view_attached = [this] (wf::signal_data_t *data)
    {
        if (!should_scale_view(get_signaled_view(data)))
        {
            return;
        }

        layout_slots(get_views());
    };

    void handle_view_disappeared(wayfire_view view)
    {
        if (scale_data.count(get_top_parent(view)) != 0)
        {
            remove_view(view);
            if (scale_data.empty())
            {
                finalize();
            }

            if (!view->parent)
            {
                layout_slots(get_views());
            }
        }
    }

    /* Destroyed view or view moved to another output */
    wf::signal_connection_t view_detached = [this] (wf::signal_data_t *data)
    {
        handle_view_disappeared(get_signaled_view(data));
    };

    /* Workspace changed */
    wf::signal_connection_t workspace_changed{[this] (wf::signal_data_t *data)
        {
            if (current_focus_view)
            {
                output->focus_view(current_focus_view, true);
            }

            layout_slots(get_views());
        }
    };

    /* View geometry changed. Also called when workspace changes */
    wf::signal_connection_t view_geometry_changed{[this] (wf::signal_data_t *data)
        {
            auto views = get_views();
            if (!views.size())
            {
                deactivate();

                return;
            }

            layout_slots(std::move(views));
        }
    };

    /* View minimized */
    wf::signal_connection_t view_minimized = [this] (wf::signal_data_t *data)
    {
        auto ev = static_cast<wf::view_minimized_signal*>(data);

        if (ev->state)
        {
            handle_view_disappeared(ev->view);
        } else if (should_scale_view(ev->view))
        {
            layout_slots(get_views());
        }
    };

    /* View unmapped */
    wf::signal_connection_t view_unmapped{[this] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);

            check_focus_view(view);
        }
    };

    /* View focused. This handler makes sure our view remains focused */
    wf::signal_connection_t view_focused = [this] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
        fade_out_all_except(view);
        fade_in(view);
        current_focus_view = view;
    };

    /* Our own refocus that uses untransformed coordinates */
    void refocus()
    {
        if (current_focus_view)
        {
            output->focus_view(current_focus_view, true);
            select_view(current_focus_view);

            return;
        }

        wayfire_view next_focus = nullptr;
        auto views = get_current_workspace_views();

        for (auto v : views)
        {
            if (v->is_mapped() &&
                v->get_keyboard_focus_surface())
            {
                next_focus = v;
                break;
            }
        }

        output->focus_view(next_focus, true);
    }

    /* Returns true if any scale animation is running */
    bool animation_running()
    {
        for (auto& e : scale_data)
        {
            if (e.second.fade_animation.running() ||
                e.second.animation.scale_animation.running())
            {
                return true;
            }
        }

        return false;
    }

    /* Assign transform values to the actual transformer */
    wf::effect_hook_t pre_hook = [=] ()
    {
        transform_views();
    };

    /* Keep rendering until all animation has finished */
    wf::effect_hook_t post_hook = [=] ()
    {
        bool running = animation_running();

        if (running)
        {
            output->render->schedule_redraw();
        }

        if (active || running)
        {
            return;
        }

        finalize();
    };

    bool can_handle_drag()
    {
        return output->is_plugin_active(this->grab_interface->name);
    }

    wf::signal_connection_t on_drag_output_focus = [=] (auto data)
    {
        auto ev = static_cast<wf::move_drag::drag_focus_output_signal*>(data);
        if ((ev->focus_output == output) && can_handle_drag())
        {
            drag_helper->set_scale(1.0);
        }
    };

    wf::signal_connection_t on_drag_done = [=] (auto data)
    {
        auto ev = static_cast<wf::move_drag::drag_done_signal*>(data);
        if ((ev->focused_output == output) && can_handle_drag())
        {
            if (ev->main_view->get_output() == ev->focused_output)
            {
                // View left on the same output, don't do anything
                for (auto& v : ev->all_views)
                {
                    set_tiled_wobbly(v.view, true);
                }

                layout_slots(get_views());
                return;
            }

            wf::move_drag::adjust_view_on_output(ev);
        }
    };

    wf::signal_connection_t on_drag_snap_off = [=] (auto data)
    {
        last_selected_view = nullptr;
    };

    /* Activate and start scale animation */
    bool activate()
    {
        if (active)
        {
            return false;
        }

        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        auto views = get_views();
        if (views.empty())
        {
            output->deactivate_plugin(grab_interface);

            return false;
        }

        initial_workspace  = output->workspace->get_current_workspace();
        initial_focus_view = output->get_active_view();
        current_focus_view = initial_focus_view ?: views.front();
        // Make sure no leftover events from the activation binding
        // trigger an action in scale
        last_selected_view = nullptr;

        if (!interact)
        {
            if (!grab_interface->grab())
            {
                deactivate();

                return false;
            }
        }

        if (current_focus_view != output->get_active_view())
        {
            output->focus_view(current_focus_view, true);
        }

        active = true;

        layout_slots(get_views());

        connect_button_signal();
        output->connect_signal("view-layer-attached", &view_attached);
        output->connect_signal("view-mapped", &view_attached);
        output->connect_signal("workspace-changed", &workspace_changed);
        output->connect_signal("view-layer-detached", &view_detached);
        output->connect_signal("view-minimized", &view_minimized);
        output->connect_signal("view-unmapped", &view_unmapped);
        output->connect_signal("view-focused", &view_focused);

        fade_out_all_except(current_focus_view);
        fade_in(current_focus_view);

        return true;
    }

    /* Deactivate and start unscale animation */
    void deactivate()
    {
        active = false;

        set_hook();
        view_focused.disconnect();
        view_unmapped.disconnect();
        view_attached.disconnect();
        view_minimized.disconnect();
        workspace_changed.disconnect();
        view_geometry_changed.disconnect();

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);

        for (auto& e : scale_data)
        {
            fade_in(e.first);
            setup_view_transform(e.second, 1, 1, 0, 0, 1);
            if (e.second.visibility == view_scale_data::view_visibility_t::HIDDEN)
            {
                e.first->set_visible(true);
            }

            e.second.visibility = view_scale_data::view_visibility_t::VISIBLE;
        }

        refocus();
        output->emit_signal("scale-end", nullptr);
    }

    /* Completely end scale, including animation */
    void finalize()
    {
        if (active)
        {
            /* only emit the signal if deactivate() was not called before */
            output->emit_signal("scale-end", nullptr);

            if (drag_helper->view)
            {
                drag_helper->handle_input_released();
            }
        }

        active = false;

        unset_hook();
        remove_transformers();
        scale_data.clear();
        grab_interface->ungrab();
        disconnect_button_signal();
        view_focused.disconnect();
        view_unmapped.disconnect();
        view_attached.disconnect();
        view_detached.disconnect();
        view_minimized.disconnect();
        workspace_changed.disconnect();
        view_geometry_changed.disconnect();
        output->deactivate_plugin(grab_interface);

        wf::stack_order_changed_signal data;
        data.output = output;
        wf::get_core().emit_signal("output-stack-order-changed", &data);
    }

    /* Utility hook setter */
    void set_hook()
    {
        if (hook_set)
        {
            return;
        }

        output->render->add_effect(&post_hook, wf::OUTPUT_EFFECT_POST);
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        output->render->schedule_redraw();
        hook_set = true;
    }

    /* Utility hook unsetter */
    void unset_hook()
    {
        if (!hook_set)
        {
            return;
        }

        output->render->rem_effect(&post_hook);
        output->render->rem_effect(&pre_hook);
        hook_set = false;
    }

    void fini() override
    {
        finalize();
        output->rem_binding(&toggle_cb);
        output->rem_binding(&toggle_all_cb);
        show_title.fini();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_scale);
