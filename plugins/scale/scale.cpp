/**
 * Original code by: Scott Moreau, Daniel Kondor
 */
#include <map>
#include <memory>
#include <wayfire/workarea.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/vswitch.hpp>
#include <wayfire/touch/touch.hpp>
#include <wayfire/plugins/scale-signal.hpp>
#include <wayfire/plugins/wobbly/wobbly-signal.hpp>
#include <wayfire/window-manager.hpp>

#include <wayfire/plugins/common/move-drag-interface.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/common/input-grab.hpp>

#include <linux/input-event-codes.h>

#include "plugins/ipc/ipc-activator.hpp"
#include "scale.hpp"
#include "scale-title-overlay.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/plugins/common/util.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/view.hpp"

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
    wf::option_wrapper_t<wf::animation_description_t> duration{"scale/duration"};
    scale_animation_t scale_animation{duration};
};

struct view_scale_data
{
    int row, col;
    std::shared_ptr<wf::scene::view_2d_transformer_t> transformer;
    wf::animation::simple_animation_t fade_animation;
    wf_scale_animation_attribs animation;
    enum class view_visibility_t
    {
        VISIBLE, /*  view is shown in position determined by layout_slots() */
        HIDING, /* view is in the process of hiding (due to filters)      */
        HIDDEN, /* view is hidden by a filter (with set_visible(false))   */
    };

    view_visibility_t visibility = view_visibility_t::VISIBLE;
    bool was_minimized = false; /* flag to indicate if this view was originally minimized */
};

/**
 * Scale has the following hard coded bindings are as follows:
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
class wayfire_scale : public wf::per_output_plugin_instance_t,
    public wf::keyboard_interaction_t,
    public wf::pointer_interaction_t,
    public wf::touch_interaction_t
{
    /* helper class for optionally showing title overlays */
    scale_show_title_t show_title;
    std::vector<int> current_row_sizes;
    wf::point_t initial_workspace;
    bool hook_set;
    /* View that was active before scale began. */
    std::weak_ptr<wf::view_interface_t> initial_focus_view;
    /* View that has active focus. */
    wayfire_toplevel_view current_focus_view;
    // View over which the last input press happened
    wayfire_toplevel_view last_selected_view;
    std::map<wayfire_toplevel_view, view_scale_data> scale_data;
    wf::option_wrapper_t<int> spacing{"scale/spacing"};
    wf::option_wrapper_t<int> outer_margin{"scale/outer_margin"};
    wf::option_wrapper_t<bool> middle_click_close{"scale/middle_click_close"};
    wf::option_wrapper_t<double> inactive_alpha{"scale/inactive_alpha"};
    wf::option_wrapper_t<double> minimized_alpha{"scale/minimized_alpha"};
    wf::option_wrapper_t<bool> allow_scale_zoom{"scale/allow_zoom"};
    wf::option_wrapper_t<bool> include_minimized{"scale/include_minimized"};
    wf::option_wrapper_t<bool> close_on_new_view{"scale/close_on_new_view"};

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

    std::unique_ptr<wf::input_grab_t> grab;

    wf::plugin_activation_data_t grab_interface{
        .name = "scale",
        .capabilities = wf::CAPABILITY_MANAGE_DESKTOP | wf::CAPABILITY_GRAB_INPUT,
        .cancel = [=] () { finalize(); },
    };

  public:
    bool active = false;

    void init() override
    {
        hook_set = false;
        grab     = std::make_unique<wf::input_grab_t>("scale", output, this, this, this);

        allow_scale_zoom.set_callback(allow_scale_zoom_option_changed);

        setup_workspace_switching();

        drag_helper->connect(&on_drag_output_focus);
        drag_helper->connect(&on_drag_done);
        drag_helper->connect(&on_drag_snap_off);

        show_title.init(output);
        output->connect(&update_cb);
    }

    void setup_workspace_switching()
    {
        workspace_bindings = std::make_unique<wf::vswitch::control_bindings_t>(output);
        workspace_bindings->setup([&] (wf::point_t delta, wayfire_toplevel_view view, bool only_view)
        {
            if (!output->is_plugin_active(grab_interface.name))
            {
                return false;
            }

            if (delta == wf::point_t{0, 0})
            {
                // Consume input event
                return true;
            }

            if (only_view)
            {
                // For now, scale does not let you move views between workspaces
                return false;
            }

            auto ws = output->wset()->get_current_workspace() + delta;

            // vswitch picks the top view, we want the focused one
            std::vector<wayfire_toplevel_view> fixed_views;
            if (view && current_focus_view && !all_workspaces)
            {
                fixed_views.push_back(current_focus_view);
            }

            output->wset()->request_workspace(ws, fixed_views);

            return true;
        });
    }

    /* Add a transformer that will be used to scale the view */
    bool add_transformer(wayfire_toplevel_view view)
    {
        if (view->get_transformed_node()->get_transformer("scale"))
        {
            return false;
        }

        auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(view);
        scale_data[view].transformer = tr;
        view->get_transformed_node()->add_transformer(tr, wf::TRANSFORMER_2D,
            "scale");
        /* Handle potentially minimized views by making them visible,
         * however, they start out as fully transparent. */
        if (view->minimized)
        {
            tr->alpha = 0.0;
            wf::scene::set_node_enabled(view->get_root_node(), true);
            scale_data[view].was_minimized = true;
        }

        /* Transformers are added only once when scale is activated so
         * this is a good place to connect the geometry-changed handler */
        view->connect(&view_geometry_changed);
        view->connect(&view_unmapped);

        set_tiled_wobbly(view, true);

        /* signal that a transformer was added to this view */
        scale_transformer_added_signal data;
        data.view = view;
        output->emit(&data);

        return true;
    }

    /* Remove the scale transformer from the view */
    void pop_transformer(wayfire_toplevel_view view)
    {
        /* signal that a transformer was added to this view */
        scale_transformer_removed_signal data;
        data.view = view;
        output->emit(&data);
        view->get_transformed_node()->rem_transformer("scale");
        view->disconnect(&view_unmapped);
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

            if (e.second.was_minimized)
            {
                wf::scene::set_node_enabled(e.first->get_root_node(), false);
            }

            if (e.second.visibility == view_scale_data::view_visibility_t::HIDDEN)
            {
                wf::scene::set_node_enabled(e.first->get_transformed_node(), true);
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

    wf::signal::connection_t<scale_update_signal> update_cb = [=] (scale_update_signal *ev)
    {
        if (active)
        {
            layout_slots(get_views());
            output->render->schedule_redraw();
        }
    };

    void handle_pointer_button(
        const wlr_pointer_button_event& event) override
    {
        process_input(event.button, event.state,
            wf::get_core().get_cursor_position());
    }

    void handle_touch_down(uint32_t, int finger_id, wf::pointf_t pos) override
    {
        if (finger_id == 0)
        {
            process_input(BTN_LEFT, WLR_BUTTON_PRESSED, pos);
        }
    }

    void handle_touch_up(uint32_t, int finger_id,
        wf::pointf_t lift_off_position) override
    {
        if (finger_id == 0)
        {
            process_input(BTN_LEFT, WLR_BUTTON_RELEASED, lift_off_position);
        }
    }

    void handle_touch_motion(uint32_t time, int finger_id,
        wf::pointf_t position) override
    {
        if (finger_id == 0)
        {
            handle_pointer_motion(position, time);
        }
    }

    /* Fade all views' alpha to inactive alpha except the
     * view argument */
    void fade_out_all_except(wayfire_toplevel_view view)
    {
        for (auto& e : scale_data)
        {
            auto v = e.first;
            if (wf::find_topmost_parent(v) == wf::find_topmost_parent(view))
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
    void fade_in(wayfire_toplevel_view view)
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
    void fade_out(wayfire_toplevel_view view)
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
            double target_alpha = (v->minimized) ? minimized_alpha : inactive_alpha;
            scale_data[v].fade_animation.animate(alpha, target_alpha);
        }
    }

    /* Switch to the workspace for the untransformed view geometry */
    void select_view(wayfire_toplevel_view view)
    {
        if (!view)
        {
            return;
        }

        auto ws = get_view_main_workspace(view);
        output->wset()->request_workspace(ws);
    }

    /* Updates current and initial view focus variables accordingly */
    void check_focus_view(wayfire_toplevel_view view)
    {
        if (view == current_focus_view)
        {
            current_focus_view = nullptr;
        }

        if (view == last_selected_view)
        {
            last_selected_view = nullptr;
        }
    }

    /* Remove transformer from view and remove view from the scale_data map */
    void remove_view(wayfire_toplevel_view view)
    {
        if (!view || !scale_data.count(view))
        {
            return;
        }

        if (scale_data.at(view).was_minimized)
        {
            wf::scene::set_node_enabled(view->get_root_node(), false);
        }

        for (auto v : view->enumerate_views(false))
        {
            check_focus_view(v);
            pop_transformer(v);
            scale_data.erase(v);
        }
    }

    /* Process button event */
    void process_input(uint32_t button, uint32_t state, wf::pointf_t input_position)
    {
        if (!active)
        {
            return;
        }

        if (state == WLR_BUTTON_PRESSED)
        {
            auto view = scale_find_view_at(input_position, output);
            if (view && should_scale_view(view))
            {
                // Mark the view as the target of the next input release operation
                last_selected_view = view;
            } else
            {
                last_selected_view = nullptr;
            }

            drag_helper->set_pending_drag(input_position);
            return;
        }

        drag_helper->handle_input_released();
        auto view = scale_find_view_at(input_position, output);
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
            fade_in(wf::find_topmost_parent(view));

            // End scale
            initial_focus_view.reset();
            deactivate();
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

    void handle_pointer_motion(wf::pointf_t to_f, uint32_t time) override
    {
        wf::point_t to{(int)std::round(to_f.x), (int)std::round(to_f.y)};
        if (!drag_helper->view && last_selected_view && drag_helper->should_start_pending_drag(to))
        {
            wf::move_drag::drag_options_t opts;
            opts.join_views = true;
            opts.enable_snap_off    = true;
            opts.snap_off_threshold = 200;

            // We want to receive raw inputs (e.g. no fake pointer releases) in case the view is moved to
            // another output.
            grab->set_wants_raw_input(true);
            drag_helper->start_drag(last_selected_view, opts);
            drag_helper->handle_motion(to);
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
    wf::point_t get_view_main_workspace(wayfire_toplevel_view view)
    {
        view = wf::find_topmost_parent(view);
        auto ws     = output->wset()->get_current_workspace();
        auto og     = output->get_layout_geometry();
        auto vg     = view->get_geometry();
        auto center = wf::point_t{vg.x + vg.width / 2, vg.y + vg.height / 2};

        return wf::point_t{
            ws.x + (int)std::floor((double)center.x / og.width),
            ws.y + (int)std::floor((double)center.y / og.height)};
    }

    /* Given row and column, return a view at this position in the scale grid,
     * or the first scaled view if none is found */
    wayfire_toplevel_view find_view_in_grid(int row, int col)
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


    void handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event ev) override
    {
        auto view = toplevel_cast(wf::get_active_view_for_output(output));
        if (!view)
        {
            view = current_focus_view;
            if (view)
            {
                fade_out_all_except(view);
                fade_in(view);

                wf::get_core().default_wm->focus_raise_view(view);
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

        if ((ev.state != WLR_KEY_PRESSED) ||
            wf::get_core().seat->get_keyboard_modifiers())
        {
            return;
        }

        switch (ev.keycode)
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
            wf::get_core().default_wm->focus_raise_view(view);

            return;

          case KEY_ESC:
            deactivate();
            output->wset()->request_workspace(initial_workspace);

            if (auto focus = initial_focus_view.lock())
            {
                wf::get_core().default_wm->focus_raise_view(focus);
            }

            initial_focus_view.reset();
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
            fade_out_all_except(view);
            fade_in(view);
            current_focus_view = view;

            // Update activated state
            wf::get_core().seat->focus_view(view);
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

            if (view_data.fade_animation.running() ||
                view_data.animation.scale_animation.running())
            {
                view->get_transformed_node()->begin_transform_update();
                view_data.transformer->scale_x =
                    view_data.animation.scale_animation.scale_x;
                view_data.transformer->scale_y =
                    view_data.animation.scale_animation.scale_y;
                view_data.transformer->translation_x =
                    view_data.animation.scale_animation.translation_x;
                view_data.transformer->translation_y =
                    view_data.animation.scale_animation.translation_y;
                view_data.transformer->alpha = view_data.fade_animation;

                if ((view_data.visibility ==
                     view_scale_data::view_visibility_t::HIDING) &&
                    !view_data.fade_animation.running())
                {
                    view_data.visibility =
                        view_scale_data::view_visibility_t::HIDDEN;
                    wf::scene::set_node_enabled(view->get_transformed_node(), false);
                }

                view->get_transformed_node()->end_transform_update();
            }
        }
    }

    /* Returns a list of views for all workspaces */
    std::vector<wayfire_toplevel_view> get_all_workspace_views()
    {
        return output->wset()->get_views(
            (include_minimized ? 0 : wf::WSET_EXCLUDE_MINIMIZED) | wf::WSET_MAPPED_ONLY);
    }

    /* Returns a list of views for the current workspace */
    std::vector<wayfire_toplevel_view> get_current_workspace_views()
    {
        std::vector<wayfire_toplevel_view> views;
        for (auto& view : get_all_workspace_views())
        {
            auto vg = view->get_geometry();
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
    std::vector<wayfire_toplevel_view> get_views()
    {
        std::vector<wayfire_toplevel_view> views;

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
    bool should_scale_view(wayfire_toplevel_view view)
    {
        auto views = get_views();

        return std::find(
            views.begin(), views.end(), wf::find_topmost_parent(view)) != views.end();
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
            wf::option_wrapper_t<wf::animation_description_t>{"scale/duration"});
        view_data.fade_animation.animate(view_data.transformer->alpha,
            target_alpha);
    }

    static bool view_compare_x(const wayfire_toplevel_view& a, const wayfire_toplevel_view& b)
    {
        auto vg_a = a->get_geometry();
        std::vector<int> a_coords = {vg_a.x, vg_a.width, vg_a.y, vg_a.height};
        auto vg_b = b->get_geometry();
        std::vector<int> b_coords = {vg_b.x, vg_b.width, vg_b.y, vg_b.height};
        return a_coords < b_coords;
    }

    static bool view_compare_y(const wayfire_toplevel_view& a, const wayfire_toplevel_view& b)
    {
        auto vg_a = a->get_geometry();
        std::vector<int> a_coords = {vg_a.y, vg_a.height, vg_a.x, vg_a.width};
        auto vg_b = b->get_geometry();
        std::vector<int> b_coords = {vg_b.y, vg_b.height, vg_b.x, vg_b.width};
        return a_coords < b_coords;
    }

    std::vector<std::vector<wayfire_toplevel_view>> view_sort(
        std::vector<wayfire_toplevel_view>& views)
    {
        std::vector<std::vector<wayfire_toplevel_view>> view_grid;
        // First ensure a consistent sorting of all views using a persistent
        // identifier before sorting by geometry.
        // This is so that if two views have exactly the same geometry,
        // they will always appear in the same order in the output list.
        std::sort(views.begin(), views.end(), [] (auto a, auto b)
        {
            return a.get() < b.get();
        });
        std::stable_sort(views.begin(), views.end(), view_compare_y);

        int rows = sqrt(views.size() + 1);
        int views_per_row = (int)std::ceil((double)views.size() / rows);
        size_t n = views.size();
        for (size_t i = 0; i < n; i += views_per_row)
        {
            size_t j = std::min(i + views_per_row, n);
            view_grid.emplace_back(views.begin() + i, views.begin() + j);
            std::stable_sort(view_grid.back().begin(), view_grid.back().end(),
                view_compare_x);
        }

        return view_grid;
    }

    /* Filter the views to be arranged by layout_slots() */
    void filter_views(std::vector<wayfire_toplevel_view>& views)
    {
        std::vector<wayfire_toplevel_view> filtered_views;
        scale_filter_signal signal(views, filtered_views);
        output->emit(&signal);

        /* update hidden views -- ensure that they and their children have a
         * transformer and are in scale_data */
        for (auto view : filtered_views)
        {
            for (auto v : view->enumerate_views(true))
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
            std::sort(views.begin(), views.end(), [=] (wayfire_toplevel_view a, wayfire_toplevel_view b)
            {
                if (a->minimized != b->minimized)
                {
                    /* avoid focusing minimized views if possible, so
                     * sort them after non-minimized ones */
                    return b->minimized;
                }

                return wf::get_focus_timestamp(a) > wf::get_focus_timestamp(b);
            });

            current_focus_view = views.empty() ? nullptr : views.front();
            wf::get_core().default_wm->focus_raise_view(current_focus_view);
        }
    }

    /* Compute target scale layout geometry for all the view transformers
     * and start animating. Initial code borrowed from the compiz scale
     * plugin algorithm */
    void layout_slots(std::vector<wayfire_toplevel_view> views)
    {
        wf::dassert(active || hook_set, "Scale is not active");
        if (!views.size())
        {
            if (!all_workspaces && active)
            {
                deactivate();
            }

            return;
        }

        filter_views(views);

        auto workarea = output->workarea->get_workarea();
        workarea.x     += outer_margin;
        workarea.y     += outer_margin;
        workarea.width -= outer_margin * 2;
        workarea.height -= outer_margin * 2;

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
                double target_alpha = 1.0;
                if (view != current_focus_view)
                {
                    target_alpha = (view->minimized) ? minimized_alpha : inactive_alpha;
                }

                // Helper function to calculate the desired scale for a view
                const auto& calculate_scale = [=] (wf::dimensions_t vg)
                {
                    double w = std::max(1.0, scaled_width);
                    double h = std::max(1.0, scaled_height);

                    const double scale = std::min(w / vg.width, h / vg.height);
                    if (!allow_scale_zoom)
                    {
                        return std::min(scale, max_scale_factor);
                    }

                    return scale;
                };

                add_transformer(view);
                auto geom = view->get_geometry();
                double view_scale = calculate_scale({geom.width, geom.height});
                for (auto& child : view->enumerate_views(true))
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
                        wf::scene::set_node_enabled(
                            child->get_transformed_node(), true);
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

                    auto vg = child->get_geometry();
                    wf::pointf_t center = {vg.x + vg.width / 2.0, vg.y + vg.height / 2.0};

                    // Take padding into account
                    double scale = calculate_scale({vg.width, vg.height});
                    // Ensure child is not scaled more than parent
                    if (!allow_scale_zoom &&
                        (child != view) &&
                        (max_scale_child > 0.0))
                    {
                        scale = std::min(max_scale_child * view_scale, scale);
                    }

                    // Target geometry is centered around the center slot
                    const double dx = x - center.x + scaled_width / 2.0;
                    const double dy = y - center.y + scaled_height / 2.0;
                    setup_view_transform(child_data, scale, scale,
                        dx, dy, target_alpha);
                }
            }
        }

        set_hook();
        transform_views();
    }

    /* Called when adding or removing a group of views to be scaled,
     * in this case between views on all workspaces and views on the
     * current workspace */
    void switch_scale_modes()
    {
        if (!output->is_plugin_active(grab_interface.name))
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
        if (!output->is_plugin_active(grab_interface.name))
        {
            return;
        }

        layout_slots(get_views());
    };

    void handle_new_view(wayfire_toplevel_view view, bool close_scale)
    {
        if (!should_scale_view(view))
        {
            return;
        }

        if (close_scale)
        {
            deactivate();
            return;
        }

        layout_slots(get_views());
    }

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            handle_new_view(toplevel, close_on_new_view);
        }
    };

    void handle_view_unmapped(wayfire_toplevel_view view)
    {
        remove_view(view);
        if (scale_data.empty())
        {
            finalize();
        } else if (!view->parent)
        {
            layout_slots(get_views());
        }
    }

    /* Workspace changed */
    wf::signal::connection_t<wf::workspace_changed_signal> workspace_changed =
        [=] (wf::workspace_changed_signal *ev)
    {
        if (current_focus_view)
        {
            wf::get_core().default_wm->focus_raise_view(current_focus_view);
        }

        layout_slots(get_views());
    };

    wf::signal::connection_t<wf::workarea_changed_signal> workarea_changed =
        [=] (wf::workarea_changed_signal *ev)
    {
        layout_slots(get_views());
    };

    /* View geometry changed. Also called when workspace changes */
    wf::signal::connection_t<wf::view_geometry_changed_signal> view_geometry_changed =
        [=] (wf::view_geometry_changed_signal *ev)
    {
        auto views = get_views();
        if (!views.size())
        {
            deactivate();

            return;
        }

        layout_slots(std::move(views));
    };

    /* View minimized */
    wf::signal::connection_t<wf::view_minimized_signal> view_minimized = [=] (wf::view_minimized_signal *ev)
    {
        // Handle view restoration, view minimization is handled by disappeared already.
        if (!ev->view->minimized)
        {
            layout_slots(get_views());
        } else if (include_minimized && scale_data.count(ev->view))
        {
            if (!scale_data.at(ev->view).was_minimized)
            {
                scale_data.at(ev->view).was_minimized = true;
                wf::scene::set_node_enabled(ev->view->get_root_node(), true);
            }

            fade_out(ev->view);
        }
    };

    /* View unmapped */
    wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            check_focus_view(toplevel);
            handle_view_unmapped(toplevel);
        }
    };

    /* Our own refocus that uses untransformed coordinates */
    void refocus()
    {
        if (current_focus_view)
        {
            wf::get_core().default_wm->focus_raise_view(current_focus_view);
            select_view(current_focus_view);

            return;
        }

        wayfire_toplevel_view next_focus = nullptr;
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

        wf::get_core().default_wm->focus_raise_view(next_focus);
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
        return output->is_plugin_active(this->grab_interface.name);
    }

    wf::signal::connection_t<wf::move_drag::drag_focus_output_signal> on_drag_output_focus =
        [=] (wf::move_drag::drag_focus_output_signal *ev)
    {
        if ((ev->focus_output == output) && can_handle_drag())
        {
            grab->set_wants_raw_input(true);
            drag_helper->set_scale(1.0);
        }
    };

    wf::signal::connection_t<wf::move_drag::drag_done_signal> on_drag_done =
        [=] (wf::move_drag::drag_done_signal *ev)
    {
        if ((ev->focused_output == output) && can_handle_drag() && !drag_helper->is_view_held_in_place())
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

        grab->set_wants_raw_input(false);
    };

    wf::signal::connection_t<wf::move_drag::snap_off_signal> on_drag_snap_off = [=] (auto)
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

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        auto views = get_views();
        if (views.empty())
        {
            output->deactivate_plugin(&grab_interface);
            return false;
        }

        initial_workspace = output->wset()->get_current_workspace();

        auto active_view = wf::get_active_view_for_output(output);
        if (active_view)
        {
            initial_focus_view = active_view->weak_from_this();
            current_focus_view = toplevel_cast(active_view);
            if (std::find(views.begin(), views.end(), current_focus_view) == views.end())
            {
                current_focus_view = nullptr;
            }
        } else
        {
            initial_focus_view.reset();
            current_focus_view = nullptr;
        }

        // Make sure no leftover events from the activation binding
        // trigger an action in scale
        last_selected_view = nullptr;

        grab->grab_input(wf::scene::layer::WORKSPACE);
        if (current_focus_view != wf::get_core().seat->get_active_view())
        {
            wf::get_core().default_wm->focus_raise_view(current_focus_view);
        }

        active = true;

        layout_slots(get_views());

        output->connect(&on_view_mapped);
        output->connect(&workspace_changed);
        output->connect(&workarea_changed);
        output->connect(&view_minimized);

        fade_out_all_except(current_focus_view);
        fade_in(current_focus_view);

        return true;
    }

    /* Deactivate and start unscale animation */
    void deactivate()
    {
        active = false;

        set_hook();
        on_view_mapped.disconnect();
        view_minimized.disconnect();
        workspace_changed.disconnect();
        workarea_changed.disconnect();
        view_geometry_changed.disconnect();

        grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);

        for (auto& e : scale_data)
        {
            if (e.first->minimized && (e.first != current_focus_view))
            {
                e.second.visibility =
                    view_scale_data::view_visibility_t::HIDING;
                setup_view_transform(e.second, 1, 1, 0, 0, 0);
            } else
            {
                fade_in(e.first);
                setup_view_transform(e.second, 1, 1, 0, 0, 1);
                if (e.second.visibility == view_scale_data::view_visibility_t::HIDDEN)
                {
                    wf::scene::set_node_enabled(e.first->get_transformed_node(), true);
                }

                e.second.visibility = view_scale_data::view_visibility_t::VISIBLE;
            }
        }

        refocus();
        scale_end_signal signal;
        output->emit(&signal);
    }

    /* Completely end scale, including animation */
    void finalize()
    {
        if (active)
        {
            /* only emit the signal if deactivate() was not called before */
            scale_end_signal signal;
            output->emit(&signal);

            if (drag_helper->view)
            {
                drag_helper->handle_input_released();
            }
        }

        active = false;

        unset_hook();
        remove_transformers();
        scale_data.clear();
        grab->ungrab_input();
        on_view_mapped.disconnect();
        view_minimized.disconnect();
        workspace_changed.disconnect();
        workarea_changed.disconnect();
        view_geometry_changed.disconnect();
        output->deactivate_plugin(&grab_interface);

        wf::scene::update(wf::get_core().scene(),
            wf::scene::update_flag::INPUT_STATE);
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
        show_title.fini();
    }
};

class wayfire_scale_global : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_scale>
{
    wf::ipc_activator_t toggle_ws{"scale/toggle"};
    wf::ipc_activator_t toggle_all{"scale/toggle_all"};

  public:
    void init() override
    {
        this->init_output_tracking();
        toggle_ws.set_handler(toggle_cb);
        toggle_all.set_handler(toggle_all_cb);
    }

    void fini() override
    {
        this->fini_output_tracking();
    }

    void handle_new_output(wf::output_t *output) override
    {
        per_output_tracker_mixin_t::handle_new_output(output);
        output->connect(&on_view_set_output);
    }

    void handle_output_removed(wf::output_t *output) override
    {
        per_output_tracker_mixin_t::handle_output_removed(output);
        output->disconnect(&on_view_set_output);
    }

    wf::signal::connection_t<wf::view_set_output_signal> on_view_set_output =
        [=] (wf::view_set_output_signal *ev)
    {
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            auto old_output = ev->output;
            if (old_output && output_instance.count(old_output))
            {
                this->output_instance[old_output]->handle_view_unmapped(toplevel);
            }

            auto new_output = ev->view->get_output();
            if (new_output && output_instance.count(new_output) && output_instance[new_output]->active)
            {
                this->output_instance[ev->view->get_output()]->handle_new_view(toplevel, false);
            }
        }
    };

    wf::ipc_activator_t::handler_t toggle_cb = [=] (wf::output_t *output, wayfire_view)
    {
        if (this->output_instance[output]->handle_toggle(false))
        {
            output->render->schedule_redraw();
            return true;
        }

        return false;
    };

    wf::ipc_activator_t::handler_t toggle_all_cb = [=] (wf::output_t *output, wayfire_view)
    {
        if (this->output_instance[output]->handle_toggle(true))
        {
            output->render->schedule_redraw();
            return true;
        }

        return false;
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_scale_global);
