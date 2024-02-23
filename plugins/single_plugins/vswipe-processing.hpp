#include <wayfire/util.hpp>
#include <cmath>

static inline double vswipe_process_delta(const double delta,
    const double accumulated_dx,
    const int vx, const int vw,
    const double speed_cap   = 0.5,
    const bool free_movement = false)
{
    // The slowdown below must be applied differently for going out of bounds.
    double sdx_offset = free_movement ?
        std::copysign(0, accumulated_dx) : accumulated_dx;
    if (vx - accumulated_dx < 0.0)
    {
        sdx_offset = (accumulated_dx - std::floor(accumulated_dx)) + 1.0;
    }

    if (vx - accumulated_dx > vw - 1.0)
    {
        sdx_offset = (accumulated_dx - std::ceil(accumulated_dx)) - 1.0;
    }

    // To achieve a "rubberband" resistance effect when going too far, ease-in
    // of the whole swiped distance is used as a slowdown factor for the current
    // delta.
    const double ease = 1.0 - std::pow(std::abs(sdx_offset) - 0.025, 4.0);

    // If we're moving further in the limit direction, slow down all the way
    // to extremely slow, but reversing the direction should be easier.
    const double slowdown = wf::clamp(ease,
        std::signbit(delta) == std::signbit(sdx_offset) ? 0.005 : 0.2, 1.0);

    return wf::clamp(delta, -speed_cap, speed_cap) * slowdown;
}

static inline int vswipe_finish_target(const double accumulated_dx,
    const int vx, const int vw,
    const double last_deltas    = 0,
    const double move_threshold = 0.35,
    const double fast_threshold = 24,
    const bool free_movement    = false)
{
    int target_dx = 0;
    if (accumulated_dx > 0)
    {
        target_dx = std::floor(accumulated_dx);
        if ((accumulated_dx - target_dx > move_threshold) ||
            ((!free_movement || !target_dx) && (last_deltas > fast_threshold)))
        {
            ++target_dx;
        }

        if (vx - target_dx < 0)
        {
            target_dx = vx;
        }
    } else if (accumulated_dx < 0)
    {
        target_dx = std::ceil(accumulated_dx);
        if ((accumulated_dx - target_dx < -move_threshold) ||
            ((!free_movement || !target_dx) && (last_deltas < -fast_threshold)))
        {
            --target_dx;
        }

        if (vx - target_dx > vw - 1)
        {
            target_dx = vx - vw + 1;
        }
    }

    if (!free_movement)
    {
        target_dx = wf::clamp(target_dx, -1, 1);
    }

    return target_dx;
}
