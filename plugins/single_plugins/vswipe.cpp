#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <wayfire/util/duration.hpp>

#include <cmath>
#include <utility>

#include <wayfire/plugins/common/workspace-wall.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>
#include "vswipe-processing.hpp"

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
        [=] (int32_t wf::geometry_t::*member, double alpha) -> int32_t
    {
        return std::round((1 - alpha) * a.*member + alpha * b.*member);
    };

    return {
        interp(&wf::geometry_t::x, xalpha),
        interp(&wf::geometry_t::y, yalpha),
        interp(&wf::geometry_t::width, xalpha),
        interp(&wf::geometry_t::height, yalpha)
    };
}

class vswipe : public wf::plugin_interface_t
{
  private:
    enum swipe_direction_t
    {
        HORIZONTAL = 1,
        VERTICAL   = 2,
        DIAGONAL   = HORIZONTAL | VERTICAL,
        UNKNOWN    = 0,
    };

    struct
    {
        bool swiping   = false;
        bool animating = false;
        swipe_direction_t direction;

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
    wf::option_wrapper_t<int> animation_duration{"vswipe/duration"};

    vswipe_smoothing_t smooth_delta{animation_duration};
    wf::option_wrapper_t<int> fingers{"vswipe/fingers"};
    wf::option_wrapper_t<double> gap{"vswipe/gap"};
    wf::option_wrapper_t<double> threshold{"vswipe/threshold"};
    wf::option_wrapper_t<double> delta_threshold{"vswipe/delta_threshold"};
    wf::option_wrapper_t<double> speed_factor{"vswipe/speed_factor"};
    wf::option_wrapper_t<double> speed_cap{"vswipe/speed_cap"};

  public:
    void init() override
    {
        grab_interface->name = "vswipe";
        grab_interface->capabilities     = wf::CAPABILITY_MANAGE_COMPOSITOR;
        grab_interface->callbacks.cancel = [=] () { finalize_and_exit(); };

        wf::get_core().connect_signal("pointer_swipe_begin", &on_swipe_begin);
        wf::get_core().connect_signal("pointer_swipe_update", &on_swipe_update);
        wf::get_core().connect_signal("pointer_swipe_end", &on_swipe_end);

        wall = std::make_unique<wf::workspace_wall_t>(output);
        wall->connect_signal("frame", &this->on_frame);
    }

    wf::signal_connection_t on_frame = {[=] (wf::signal_data_t*)
        {
            if (!smooth_delta.running() && !state.swiping)
            {
                finalize_and_exit();

                return;
            }

            output->render->schedule_redraw();

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

            wall->set_viewport(interpolate(g1, g2, -smooth_delta.dx,
                -smooth_delta.dy));
        }
    };

    template<class wlr_event> using event = wf::input_event_signal<wlr_event>;
    wf::signal_callback_t on_swipe_begin = [=] (wf::signal_data_t *data)
    {
        if (!enable_horizontal && !enable_vertical)
        {
            return;
        }

        if (output->is_plugin_active(grab_interface->name))
        {
            return;
        }

        auto ev = static_cast<
            event<wlr_pointer_swipe_begin_event>*>(data)->event;
        if (static_cast<int>(ev->fingers) != fingers)
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
        auto grid = output->workspace->get_workspace_grid_size();
        auto ws   = output->workspace->get_current_workspace();
        state.vw = grid.width;
        state.vh = grid.height;
        state.vx = ws.x;
        state.vy = ws.y;
    };

    void start_swipe(swipe_direction_t direction)
    {
        assert(direction != UNKNOWN);
        state.direction = direction;

        if (!output->activate_plugin(grab_interface))
        {
            return;
        }

        grab_interface->grab();
        wf::get_core().focus_output(output);

        auto ws = output->workspace->get_current_workspace();
        wall->set_background_color(background_color);
        wall->set_gap_size(gap);
        wall->set_viewport(wall->get_workspace_rectangle(ws));
        wall->start_output_renderer();
    }

    // XXX: how to determine this??
    static constexpr double initial_direction_threshold = 0.05;
    static constexpr double secondary_direction_threshold = 0.3;
    static constexpr double diagonal_threshold = 1.73; // tan(30deg)
    bool is_diagonal(wf::pointf_t deltas)
    {
        /* Diagonal movement is possible if the slope is not too steep
         * and we have moved enough */
        double slope  = deltas.x / deltas.y;
        bool diagonal = wf::clamp(slope,
            1.0 / diagonal_threshold, diagonal_threshold) == slope;
        diagonal &= (deltas.x * deltas.x + deltas.y * deltas.y) >=
            initial_direction_threshold * initial_direction_threshold;

        return diagonal;
    }

    swipe_direction_t calculate_direction(wf::pointf_t deltas)
    {
        auto grid = output->workspace->get_workspace_grid_size();

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

    wf::signal_callback_t on_swipe_update = [&] (wf::signal_data_t *data)
    {
        if (!state.swiping)
        {
            return;
        }

        auto ev = static_cast<
            event<wlr_pointer_swipe_update_event>*>(data)->event;

        state.delta_sum.x += ev->dx / speed_factor;
        state.delta_sum.y += ev->dy / speed_factor;
        if (state.direction == UNKNOWN)
        {
            state.initial_deltas.x +=
                std::abs(ev->dx) / speed_factor;
            state.initial_deltas.y +=
                std::abs(ev->dy) / speed_factor;

            state.direction = calculate_direction(state.initial_deltas);
            if (state.direction == UNKNOWN)
            {
                return;
            }

            start_swipe(state.direction);
        } else if ((state.direction != DIAGONAL) && enable_free_movement)
        {
            /* Consider promoting to diagonal movement */
            double other = (state.direction == HORIZONTAL ?
                state.delta_sum.y : state.delta_sum.x);
            if (std::abs(other) > secondary_direction_threshold)
            {
                state.direction = DIAGONAL;
            }
        }

        const double cap = speed_cap;
        const double fac = speed_factor;

        state.delta_prev = state.delta_last;
        double current_delta_processed;

        const auto& process_delta = [&] (double delta,
                                         wf::timed_transition_t& total_delta, int ws,
                                         int ws_max)
        {
            current_delta_processed = vswipe_process_delta(delta, total_delta,
                ws, ws_max, cap, fac, enable_free_movement);

            double new_delta_end   = total_delta.end + current_delta_processed;
            double new_delta_start =
                smooth_transition ? total_delta : new_delta_end;
            total_delta.set(new_delta_start, new_delta_end);
        };

        if (state.direction & HORIZONTAL)
        {
            process_delta(ev->dx, smooth_delta.dx, state.vx, state.vw);
        }

        if (state.direction & VERTICAL)
        {
            process_delta(ev->dy, smooth_delta.dy, state.vy, state.vh);
        }

        state.delta_last = {ev->dx, ev->dy};
        smooth_delta.start();
    };

    wf::signal_callback_t on_swipe_end = [=] (wf::signal_data_t *data)
    {
        if (!state.swiping || !output->is_plugin_active(grab_interface->name))
        {
            state.swiping = false;

            return;
        }

        state.swiping = false;
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
        output->workspace->set_workspace(target_workspace);
        state.animating = true;
    };

    void finalize_and_exit()
    {
        state.swiping = false;
        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
        wall->stop_output_renderer(true);
        state.animating = false;
    }

    void fini() override
    {
        if (state.swiping)
        {
            finalize_and_exit();
        }

        wf::get_core().disconnect_signal("pointer_swipe_begin", &on_swipe_begin);
        wf::get_core().disconnect_signal("pointer_swipe_update", &on_swipe_update);
        wf::get_core().disconnect_signal("pointer_swipe_end", &on_swipe_end);
    }
};

DECLARE_WAYFIRE_PLUGIN(vswipe);
