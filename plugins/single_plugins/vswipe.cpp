#include <memory>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <wayfire/util/duration.hpp>

#include <cmath>

#include <wayfire/plugins/common/workspace-wall.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>
#include "vswipe-processing.hpp"
#include "wayfire/plugins/common/input-grab.hpp"
#include "wayfire/signal-provider.hpp"
#include <wayfire/output-layout.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/toplevel-view.hpp>
#include <cctype>
#include <map>
#include <string>

using namespace wf::animation;
class vswipe_smoothing_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t dx{*this};
    timed_transition_t dy{*this};
};

static inline wf::geometry_t interpolate(wf::geometry_t a, wf::geometry_t b,
    double xalpha, double yalpha)
{
    const auto& interp =
        [=] (double wf::geometry_t::*member, double alpha) -> double
    {
        return (1 - alpha) * a.*member + alpha * b.*member;
    };

    return {
        interp(&wf::geometry_t::x, xalpha),
        interp(&wf::geometry_t::y, yalpha),
        interp(&wf::geometry_t::width, xalpha),
        interp(&wf::geometry_t::height, yalpha)
    };
}

/**
 * Parse a modifier key combination (e.g. "<alt><shift><super>") into a WLR_MODIFIER_* bitmask. Unrecognized
 * tokens are ignored.
 */
static uint32_t parse_modifier_mask(std::string description)
{
    static const std::map<std::string, uint32_t> modifier_names =
    {
        {"shift", WLR_MODIFIER_SHIFT},
        {"ctrl", WLR_MODIFIER_CTRL},
        {"alt", WLR_MODIFIER_ALT},
        {"super", WLR_MODIFIER_LOGO},
    };

    uint32_t mask = 0;
    std::string token;
    description += ' ';
    for (auto c : description)
    {
        if ((c == '<') || (c == '>') || std::isspace(c))
        {
            auto it = modifier_names.find(token);
            if (it != modifier_names.end())
            {
                mask |= it->second;
            }

            token.clear();
        } else
        {
            token += std::tolower(c);
        }
    }

    return mask;
}

class vswipe : public wf::per_output_plugin_instance_t
{
  private:
    enum swipe_direction_t
    {
        HORIZONTAL = 1,
        VERTICAL   = 2,
        DIAGONAL   = HORIZONTAL | VERTICAL,
        UNKNOWN    = 0,
    };

    enum swipe_mode_t
    {
        MODE_VIEWPORT,
        MODE_OUTPUT,
    };

    struct
    {
        bool swiping   = false;
        bool animating = false;
        swipe_direction_t direction;
        swipe_mode_t mode = MODE_VIEWPORT;

        wf::pointf_t initial_deltas;
        wf::pointf_t delta_sum;

        wf::pointf_t delta_prev;
        wf::pointf_t delta_last;

        int vx = 0;
        int vy = 0;
        int vw = 0;
        int vh = 0;
    } state;

    std::unique_ptr<wf::workspace_wall_t> wall;
    wf::option_wrapper_t<bool> enable_horizontal{"vswipe/enable_horizontal"};
    wf::option_wrapper_t<bool> enable_vertical{"vswipe/enable_vertical"};
    wf::option_wrapper_t<bool> enable_free_movement{"vswipe/enable_free_movement"};
    wf::option_wrapper_t<bool> smooth_transition{"vswipe/enable_smooth_transition"};

    wf::option_wrapper_t<wf::color_t> background_color{"vswipe/background"};
    wf::option_wrapper_t<wf::animation_description_t> animation_duration{"vswipe/duration"};

    vswipe_smoothing_t smooth_delta{animation_duration};
    wf::option_wrapper_t<int> viewport_fingers{"vswipe/viewport_fingers"};
    wf::option_wrapper_t<int> output_fingers{"vswipe/output_fingers"};
    wf::option_wrapper_t<std::string> window_modifier{"vswipe/window_modifier"};
    wf::option_wrapper_t<double> gap{"vswipe/gap"};
    wf::option_wrapper_t<double> threshold{"vswipe/threshold"};
    wf::option_wrapper_t<double> delta_threshold{"vswipe/delta_threshold"};
    wf::option_wrapper_t<double> speed_factor{"vswipe/speed_factor"};
    wf::option_wrapper_t<double> speed_cap{"vswipe/speed_cap"};
    std::unique_ptr<wf::input_grab_t> input_grab;
    wf::wl_idle_call idle_switch_output;
    wf::plugin_activation_data_t grab_interface = {
        .name = "vswipe",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
        .cancel = [=] () { finalize_and_exit(); },
    };

  public:
    void init() override
    {
        input_grab = std::make_unique<wf::input_grab_t>("vswipe", output);

        wf::get_core().connect(&on_swipe_begin);
        wf::get_core().connect(&on_swipe_update);
        wf::get_core().connect(&on_swipe_end);

        wall = std::make_unique<wf::workspace_wall_t>(output);
        wall->connect(&this->on_frame);
    }

    wf::effect_hook_t post_frame = [=] ()
    {
        if (!smooth_delta.running() && !state.swiping)
        {
            finalize_and_exit();
            return;
        }

        output->render->schedule_redraw();
        output->render->damage_whole();
    };

    wf::signal::connection_t<wf::wall_frame_event_t> on_frame = [=] (wf::wall_frame_event_t *ev)
    {
        wf::point_t current_workspace = {state.vx, state.vy};
        int dx = 0, dy = 0;

        if (state.direction & HORIZONTAL)
        {
            dx = 1;
        }

        if (state.direction & VERTICAL)
        {
            dy = 1;
        }

        wf::point_t next_ws =
        {current_workspace.x + dx, current_workspace.y + dy};
        auto g1 = wall->get_workspace_rectangle(current_workspace);
        auto g2 = wall->get_workspace_rectangle(next_ws);
        wall->set_viewport(interpolate(g1, g2, -smooth_delta.dx, -smooth_delta.dy));
    };

    template<class wlr_event> using event = wf::input_event_signal<wlr_event>;
    wf::signal::connection_t<event<wlr_pointer_swipe_begin_event>> on_swipe_begin =
        [=] (event<wlr_pointer_swipe_begin_event> *ev)
    {
        if (!enable_horizontal && !enable_vertical)
        {
            return;
        }

        if (output->is_plugin_active(grab_interface.name))
        {
            return;
        }

        const int finger_count = ev->event->fingers;
        if ((viewport_fingers > 0) && (finger_count == viewport_fingers))
        {
            state.mode = MODE_VIEWPORT;
        } else if ((output_fingers > 0) && (finger_count == output_fingers))
        {
            state.mode = MODE_OUTPUT;
        } else
        {
            return;
        }

        // Plugins are per output, swipes are global, so we need to handle
        // the swipe only when the cursor is on *our* (plugin instance's) output
        if (!(output->get_relative_geometry() & output->get_cursor_position()))
        {
            return;
        }

        state.swiping   = true;
        state.direction = UNKNOWN;
        state.initial_deltas = {0.0, 0.0};
        smooth_delta.dx.set(0, 0);
        smooth_delta.dy.set(0, 0);

        state.delta_last = {0, 0};
        state.delta_prev = {0, 0};
        state.delta_sum  = {0, 0};

        // We switch the actual workspace before the finishing animation,
        // so the rendering of the animation cannot dynamically query current
        // workspace again, so it's stored here
        auto grid = output->wset()->get_workspace_grid_size();
        auto ws   = output->wset()->get_current_workspace();
        state.vw = grid.width;
        state.vh = grid.height;
        state.vx = ws.x;
        state.vy = ws.y;
    };

    void start_swipe(swipe_direction_t direction)
    {
        assert(direction != UNKNOWN);
        state.direction = direction;

        if (!output->activate_plugin(&grab_interface))
        {
            return;
        }

        input_grab->grab_input(wf::scene::layer::OVERLAY);
        wf::get_core().seat->focus_output(output);

        auto ws = output->wset()->get_current_workspace();
        wall->set_background_color(background_color);
        wall->set_gap_size(gap);
        wall->set_viewport(wall->get_workspace_rectangle(ws));
        wall->start_output_renderer();
        output->render->add_effect(&post_frame, wf::OUTPUT_EFFECT_POST);
    }

    // XXX: how to determine this??
    static constexpr double initial_direction_threshold   = 0.05;
    static constexpr double secondary_direction_threshold = 0.3;
    static constexpr double diagonal_threshold = 1.73; // tan(30deg)
    bool is_diagonal(wf::pointf_t deltas)
    {
        /* Diagonal movement is possible if the slope is not too steep and we have moved enough */
        double slope  = deltas.x / deltas.y;
        bool diagonal = wf::clamp(slope,
            1.0 / diagonal_threshold, diagonal_threshold) == slope;
        diagonal &= (deltas.x * deltas.x + deltas.y * deltas.y) >=
            initial_direction_threshold * initial_direction_threshold;

        return diagonal;
    }

    swipe_direction_t calculate_direction(wf::pointf_t deltas)
    {
        auto grid = output->wset()->get_workspace_grid_size();

        bool horizontal = deltas.x > initial_direction_threshold;
        bool vertical   = deltas.y > initial_direction_threshold;

        horizontal &= deltas.x > deltas.y;
        vertical   &= deltas.y > deltas.x;

        if (is_diagonal(deltas) && enable_free_movement)
        {
            return DIAGONAL;
        } else if (horizontal && (grid.width > 1) && enable_horizontal)
        {
            return HORIZONTAL;
        } else if (vertical && (grid.height > 1) && enable_vertical)
        {
            return VERTICAL;
        }

        return UNKNOWN;
    }

    wf::signal::connection_t<event<wlr_pointer_swipe_update_event>> on_swipe_update =
        [&] (event<wlr_pointer_swipe_update_event> *ev)
    {
        if (!state.swiping)
        {
            return;
        }

        state.delta_sum.x += ev->event->dx / speed_factor;
        state.delta_sum.y += ev->event->dy / speed_factor;
        if (state.mode == MODE_OUTPUT)
        {
            return;
        }

        if (state.direction == UNKNOWN)
        {
            state.initial_deltas.x += std::abs(ev->event->dx / speed_factor);
            state.initial_deltas.y += std::abs(ev->event->dy / speed_factor);
            state.direction = calculate_direction(state.initial_deltas);
            if (state.direction == UNKNOWN)
            {
                return;
            }

            start_swipe(state.direction);
        } else if ((state.direction != DIAGONAL) && enable_free_movement)
        {
            /* Consider promoting to diagonal movement */
            double other = (state.direction == HORIZONTAL ? state.delta_sum.y : state.delta_sum.x);
            if (std::abs(other) > secondary_direction_threshold)
            {
                state.direction = DIAGONAL;
            }
        }

        const double cap = speed_cap;

        state.delta_prev = state.delta_last;
        double current_delta_processed;

        const auto& process_delta =
            [&] (double delta, wf::timed_transition_t& total_delta, int ws, int ws_max)
        {
            current_delta_processed = vswipe_process_delta(delta / speed_factor, total_delta,
                ws, ws_max, cap, enable_free_movement);

            double new_delta_end   = total_delta.end + current_delta_processed;
            double new_delta_start = smooth_transition ? total_delta : new_delta_end;
            total_delta.set(new_delta_start, new_delta_end);
        };

        if (state.direction & HORIZONTAL)
        {
            process_delta(ev->event->dx, smooth_delta.dx, state.vx, state.vw);
        }

        if (state.direction & VERTICAL)
        {
            process_delta(ev->event->dy, smooth_delta.dy, state.vy, state.vh);
        }

        state.delta_last = {ev->event->dx, ev->event->dy};
        smooth_delta.start();
    };

    wf::signal::connection_t<event<wlr_pointer_swipe_end_event>> on_swipe_end = [=] (auto)
    {
        if (!state.swiping)
        {
            return;
        }

        state.swiping = false;

        if (state.mode == MODE_OUTPUT)
        {
            handle_output_swipe();
            return;
        }

        if (!output->is_plugin_active(grab_interface.name))
        {
            return;
        }

        const double move_threshold = wf::clamp((double)threshold, 0.0, 1.0);
        const double fast_threshold =
            wf::clamp((double)delta_threshold, 0.0, 1000.0);

        wf::point_t target_delta     = {0, 0};
        wf::point_t target_workspace = {state.vx, state.vy};

        if (state.direction & HORIZONTAL)
        {
            target_delta.x = vswipe_finish_target(smooth_delta.dx.end,
                state.vx, state.vw, state.delta_prev.x + state.delta_last.x,
                move_threshold, fast_threshold, enable_free_movement);
            target_workspace.x -= target_delta.x;
        }

        if (state.direction & VERTICAL)
        {
            target_delta.y = vswipe_finish_target(smooth_delta.dy.end,
                state.vy, state.vh, state.delta_prev.y + state.delta_last.y,
                move_threshold, fast_threshold, enable_free_movement);
            target_workspace.y -= target_delta.y;
        }

        smooth_delta.dx.restart_with_end(target_delta.x);
        smooth_delta.dy.restart_with_end(target_delta.y);
        smooth_delta.start();
        output->wset()->set_workspace(target_workspace);
        if ((target_workspace.x != state.vx) || (target_workspace.y != state.vy))
        {
            if (is_window_modifier_pressed())
            {
                move_active_window({state.vx, state.vy}, target_workspace);
            }
        }

        state.animating = true;
    };

    bool is_window_modifier_pressed()
    {
        const auto mask = parse_modifier_mask(window_modifier);
        if (mask == 0)
        {
            return false;
        }

        return (wf::get_core().seat->get_keyboard_modifiers() & mask) == mask;
    }

    /**
     * Move the topmost active view of this output (together with its children) from the workspace 'from' to
     * the workspace 'to'.
     */
    void move_active_window(wf::point_t from, wf::point_t to)
    {
        auto view = wf::find_topmost_parent(
            wf::toplevel_cast(wf::get_active_view_for_output(output)));
        if (!view || view->sticky)
        {
            return;
        }

        auto ws_size = wall->get_workspace_rectangle({0, 0});
        const int dx = (to.x - from.x) * ws_size.width;
        const int dy = (to.y - from.y) * ws_size.height;
        for (auto& v : view->enumerate_views())
        {
            auto g = v->get_pending_geometry();
            v->move(g.x + dx, g.y + dy);
        }
    }

    wf::output_t *get_output_in_direction(int dir_x, int dir_y)
    {
        auto current_output = wf::get_core().seat->get_active_output();
        if (!current_output)
        {
            return nullptr;
        }

        auto current_geo = current_output->get_layout_geometry();
        wf::pointf_t current_center = {
            current_geo.x + current_geo.width / 2.0,
            current_geo.y + current_geo.height / 2.0
        };

        wf::output_t *best_output = nullptr;
        double best_score = -INFINITY;

        const int MIN_OVERLAP = 20;

        for (auto& other_output : wf::get_core().output_layout->get_outputs())
        {
            if (other_output == current_output)
            {
                continue;
            }

            auto geo = other_output->get_layout_geometry();
            wf::pointf_t center = {
                geo.x + geo.width / 2.0,
                geo.y + geo.height / 2.0
            };

            double dx = center.x - current_center.x;
            double dy = center.y - current_center.y;

            if (((dir_x != 0) && (dx * dir_x <= 0)) ||
                ((dir_y != 0) && (dy * dir_y <= 0)))
            {
                continue;
            }

            double ortho_overlap = 1.0;
            if (dir_x != 0)
            {
                int current_top    = current_geo.y;
                int current_bottom = current_geo.y + current_geo.height;
                int other_top    = geo.y;
                int other_bottom = geo.y + geo.height;

                int overlap = std::min(current_bottom, other_bottom) -
                    std::max(current_top, other_top);
                ortho_overlap = (double)overlap / current_geo.height;
            } else if (dir_y != 0)
            {
                int current_left  = current_geo.x;
                int current_right = current_geo.x + current_geo.width;
                int other_left    = geo.x;
                int other_right   = geo.x + geo.width;

                int overlap = std::min(current_right, other_right) -
                    std::max(current_left, other_left);
                ortho_overlap = (double)overlap / current_geo.width;
            }

            if (ortho_overlap * 100 < MIN_OVERLAP)
            {
                continue;
            }

            double distance = sqrt(dx * dx + dy * dy);
            double score    = ortho_overlap / distance;

            if (score > best_score)
            {
                best_output = other_output;
                best_score  = score;
            }
        }

        return best_output ? best_output : current_output;
    }

    void switch_to_output(wf::output_t *target_output)
    {
        if (!target_output)
        {
            LOGI("No output found in requested direction. Cannot switch.");
            return;
        }

        /* when we switch the output, the swipe may be activated again for the next output, which we don't
         * want, so we postpone the switch */
        idle_switch_output.run_once([=] ()
        {
            wf::get_core().seat->focus_output(target_output);
            target_output->ensure_pointer(true);
        });
    }

    void switch_to_output_with_window(wf::output_t *target_output)
    {
        auto current_output = wf::get_core().seat->get_active_output();
        auto view =
            wf::find_topmost_parent(wf::toplevel_cast(wf::get_active_view_for_output(current_output)));
        if (view)
        {
            move_view_to_output(view, target_output, true);
        }

        switch_to_output(target_output);
    }

    /**
     * Handle a swipe which was started with the output-switching finger count: switch to the output in the
     * swipe direction (swiping right switches to the output on the right), moving the active window along if
     * the window modifier is pressed.
     */
    void handle_output_swipe()
    {
        const double move_threshold = wf::clamp((double)threshold, 0.0, 1.0);

        /* For output switching the target is in the swipe direction itself, i.e. swiping right switches to
         * the output on the right */
        const double dx = state.delta_sum.x;
        const double dy = state.delta_sum.y;

        int dir_x = 0, dir_y = 0;
        if (std::abs(dx) >= std::abs(dy))
        {
            if (enable_horizontal && (std::abs(dx) > move_threshold))
            {
                dir_x = (dx > 0) ? 1 : -1;
            }
        } else if (enable_vertical && (std::abs(dy) > move_threshold))
        {
            dir_y = (dy > 0) ? 1 : -1;
        }

        if (!dir_x && !dir_y)
        {
            return;
        }

        auto target_output  = get_output_in_direction(dir_x, dir_y);
        auto current_output = wf::get_core().seat->get_active_output();
        if (!target_output || (target_output == current_output))
        {
            return;
        }

        if (is_window_modifier_pressed())
        {
            switch_to_output_with_window(target_output);
        } else
        {
            switch_to_output(target_output);
        }
    }

    void finalize_and_exit()
    {
        state.swiping = false;
        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);
        wall->stop_output_renderer(true);
        output->render->rem_effect(&post_frame);
        state.animating = false;
    }

    void fini() override
    {
        if (state.swiping)
        {
            finalize_and_exit();
        }

        idle_switch_output.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<vswipe>);
