#include "adaptive-repaint-scheduler.hpp"

#include <algorithm>
#include <cstdlib>

namespace wf
{
namespace
{
constexpr int64_t NSEC_PER_MSEC = 1'000'000;
/** Covers timestamp and submission overhead not represented by a duration sample. */
constexpr int64_t SAMPLE_MARGIN_NS = 250'000;
/** Added after a miss; vblank-quantized lateness is not the budget shortfall. */
constexpr int64_t MISS_GUARD_STEP_NS = 500'000;
/** Removed per successful rendered frame, recovering one miss after ten successes. */
constexpr int64_t SUCCESS_GUARD_STEP_NS = 50'000;
/** Maximum accepted lead of a hardware presentation timestamp over CLOCK_MONOTONIC. */
constexpr int64_t MAX_FUTURE_ANCHOR_NS = 1'000'000;
constexpr size_t MAX_PENDING_FRAMES    = 16;
}

adaptive_repaint_scheduler_t::estimator_t& adaptive_repaint_scheduler_t::get_estimator(
    repaint_path_t path)
{
    return path == repaint_path_t::DIRECT_SCANOUT ? scanout : composed;
}

const adaptive_repaint_scheduler_t::estimator_t& adaptive_repaint_scheduler_t::get_estimator(
    repaint_path_t path) const
{
    return path == repaint_path_t::DIRECT_SCANOUT ? scanout : composed;
}

repaint_path_t adaptive_repaint_scheduler_t::predict_path() const
{
    // A single scanout may be transient. Only use its smaller budget once the
    // output has remained in scanout for several consecutive presentations.
    return consecutive_scanouts >= 3 ? repaint_path_t::DIRECT_SCANOUT : repaint_path_t::COMPOSED;
}

int64_t adaptive_repaint_scheduler_t::get_budget_ns(
    repaint_path_t path, int min_render_budget_ms) const
{
    const int64_t configured_floor = std::max(0, min_render_budget_ms) * NSEC_PER_MSEC;
    const auto& estimator = get_estimator(path);
    if (estimator.paint_budget_ns == 0)
    {
        return std::max(configured_floor, refresh_ns);
    }

    return std::max(configured_floor, estimator.paint_budget_ns + estimator.miss_guard_ns);
}

repaint_scheduler_debug_info_t adaptive_repaint_scheduler_t::get_debug_info(
    int min_render_budget_ms) const
{
    auto path_info = [=] (repaint_path_t path)
    {
        const auto& estimator = get_estimator(path);
        return render_path_debug_info_t{
            .paint_budget_ns = estimator.paint_budget_ns,
            .miss_guard_ns   = estimator.miss_guard_ns,
            .total_budget_ns = get_budget_ns(path, min_render_budget_ms),
            .successful_presentations = estimator.successful_presentations,
        };
    };

    return repaint_scheduler_debug_info_t{
        .has_last_presentation = last_presentation_ns.has_value(),
        .last_presentation_ns  = last_presentation_ns.value_or(0),
        .refresh_ns     = refresh_ns,
        .pending_frames = static_cast<uint32_t>(pending_frames.size()),
        .consecutive_scanouts = consecutive_scanouts,
        .predicted_path = predict_path(),
        .composed = path_info(repaint_path_t::COMPOSED),
        .direct_scanout = path_info(repaint_path_t::DIRECT_SCANOUT),
    };
}

repaint_schedule_t adaptive_repaint_scheduler_t::schedule_frame(
    int64_t now_ns, int min_render_budget_ms, bool dynamic_delay, bool vrr_enabled) const
{
    repaint_schedule_t result;
    result.predicted_path = predict_path();

    if (((min_render_budget_ms < 0) && !dynamic_delay) || !last_presentation_ns || (refresh_ns <= 0))
    {
        return result;
    }

    const int64_t raw_age_ns = now_ns - *last_presentation_ns;
    result.presentation_anchor_age_ns = raw_age_ns;
    if (raw_age_ns < -MAX_FUTURE_ANCHOR_NS)
    {
        return result;
    }

    const int64_t age_ns = std::max<int64_t>(0, raw_age_ns);
    result.presentation_anchor_clamped = raw_age_ns < 0;

    if (vrr_enabled)
    {
        // There is no fixed presentation phase to extrapolate after VRR goes idle.
        if (age_ns > refresh_ns)
        {
            return result;
        }

        result.target_presentation_ns = *last_presentation_ns + refresh_ns;
    } else
    {
        // Preserve the fixed-refresh phase across idle periods and select the
        // first presentation slot strictly after now.
        const int64_t periods_to_target = age_ns / refresh_ns + 1;
        result.target_presentation_ns = *last_presentation_ns + periods_to_target * refresh_ns;
    }

    int64_t budget_ns = min_render_budget_ms * NSEC_PER_MSEC;
    if (dynamic_delay)
    {
        budget_ns = get_budget_ns(result.predicted_path, min_render_budget_ms);
    }

    // VRR has no fixed missed-vblank semantics, but pacing an active stream to
    // the mode's fastest interval avoids immediate, irregular back-to-back flips.
    const int64_t delay_ns = *result.target_presentation_ns - budget_ns - now_ns;
    if (delay_ns > 0)
    {
        result.repaint_deadline_ns = now_ns + delay_ns;
        result.delay_ns = delay_ns;
    }

    return result;
}

void adaptive_repaint_scheduler_t::update_duration(estimator_t& estimator, int64_t duration_ns)
{
    const int64_t sample = std::max<int64_t>(0, duration_ns) + SAMPLE_MARGIN_NS;
    if ((estimator.paint_budget_ns == 0) || (sample > estimator.paint_budget_ns))
    {
        estimator.paint_budget_ns = sample;
    } else
    {
        // Decay slowly enough to retain protection against occasional expensive frames.
        estimator.paint_budget_ns -= (estimator.paint_budget_ns - sample) / 64;
    }
}

void adaptive_repaint_scheduler_t::raise_duration_estimate(
    estimator_t& estimator, int64_t duration_ns)
{
    const int64_t sample = std::max<int64_t>(0, duration_ns) + SAMPLE_MARGIN_NS;
    if (sample > estimator.paint_budget_ns)
    {
        estimator.paint_budget_ns = sample;
    }
}

void adaptive_repaint_scheduler_t::observe_render_completion(
    uint32_t commit_seq, repaint_path_t path, int64_t duration_ns)
{
    raise_duration_estimate(get_estimator(path), duration_ns);

    auto pending = std::find_if(pending_frames.begin(), pending_frames.end(), [&] (const auto& frame)
    {
        return (frame.commit_seq == commit_seq) && (frame.path == path);
    });

    if (pending != pending_frames.end())
    {
        pending->render_completion_ns = pending->paint_started_ns +
            std::max<int64_t>(0, duration_ns);
    }
}

void adaptive_repaint_scheduler_t::submit_frame(const repaint_schedule_t& schedule,
    repaint_path_t path, int64_t paint_started_ns, int64_t committed_ns,
    uint32_t commit_seq, bool vrr_enabled)
{
    update_duration(get_estimator(path), committed_ns - paint_started_ns);

    if (path == repaint_path_t::DIRECT_SCANOUT)
    {
        consecutive_scanouts = last_path == path ? consecutive_scanouts + 1 : 1;
    } else
    {
        consecutive_scanouts = 0;
    }

    last_path = path;
    pending_frames.push_back({commit_seq, path, schedule.target_presentation_ns,
        path == repaint_path_t::DIRECT_SCANOUT ?
        std::optional<int64_t>{committed_ns} : std::nullopt,
        paint_started_ns, refresh_ns, vrr_enabled});
    if (pending_frames.size() > MAX_PENDING_FRAMES)
    {
        pending_frames.pop_front();
    }
}

void adaptive_repaint_scheduler_t::record_miss(estimator_t& estimator, int64_t period_ns)
{
    estimator.miss_guard_ns = std::min(period_ns,
        estimator.miss_guard_ns + MISS_GUARD_STEP_NS);
    estimator.successful_presentations = 0;
}

void adaptive_repaint_scheduler_t::record_success(estimator_t& estimator,
    std::optional<int64_t> completion_slack_ns)
{
    if (estimator.successful_presentations < UINT32_MAX)
    {
        ++estimator.successful_presentations;
    }

    if (!completion_slack_ns)
    {
        estimator.miss_guard_ns = std::max<int64_t>(0,
            estimator.miss_guard_ns - SUCCESS_GUARD_STEP_NS);
        return;
    }

    // A successful frame tells us that the combination of paint budget + miss guard was sufficient.
    // We can decrease the miss guard, if the current completion slack is smaller than the last miss guard.
    //
    // Otherwise, we don't know whether a smaller guard wouldn't cause missed frames, so we keep the old
    // guard value.
    //
    // It is possible that the completion slack is even larger than the current guard, but in this case we
    // avoid lowering our guard, because the needed guard fluctuates over time and we try to avoid
    // oscillations.
    const int64_t supported_guard_ns = std::max<int64_t>(0,
        *completion_slack_ns - SAMPLE_MARGIN_NS);
    if (supported_guard_ns < estimator.miss_guard_ns)
    {
        estimator.miss_guard_ns = std::max(supported_guard_ns,
            estimator.miss_guard_ns - SUCCESS_GUARD_STEP_NS);
    }
}

void adaptive_repaint_scheduler_t::handle_presentation(const repaint_presentation_t& event)
{
    auto pending = std::find_if(pending_frames.begin(), pending_frames.end(), [&] (const auto& frame)
    {
        return frame.commit_seq == event.commit_seq;
    });

    if (!event.presented)
    {
        if (pending != pending_frames.end())
        {
            pending_frames.erase(pending);
        }

        last_presentation_ns.reset();
        return;
    }

    if ((event.when_ns <= 0) || (event.refresh_ns <= 0))
    {
        if (pending != pending_frames.end())
        {
            pending_frames.erase(pending);
        }

        last_presentation_ns.reset();
        refresh_ns = 0;
        return;
    }

    last_presentation_ns = event.when_ns;
    refresh_ns = event.refresh_ns;

    if (pending == pending_frames.end())
    {
        return;
    }

    if (event.timing_reliable && !pending->vrr_enabled && pending->target_presentation_ns)
    {
        const int64_t lateness  = event.when_ns - *pending->target_presentation_ns;
        const int64_t tolerance = std::max<int64_t>(250'000, pending->refresh_ns / 3);
        std::optional<int64_t> completion_slack_ns;
        if (pending->render_completion_ns)
        {
            completion_slack_ns = *pending->target_presentation_ns -
                *pending->render_completion_ns;
        }

        if (lateness > tolerance)
        {
            // If rendering completes after the target presentation, we know that the paint budget itself
            // was underestimated. In these cases, we don't record a miss: the paint budget estimation should
            // be updated in update_duration / raise_duration_estimate.
            //
            // However, if rendering completed before the target presentation, the paint budget was enough,
            // but we need to increase the miss guard, therefore we record a miss.
            if (!pending->render_completion_ns || (*pending->render_completion_ns <
                                                   *pending->target_presentation_ns))
            {
                record_miss(get_estimator(pending->path), pending->refresh_ns);
            }
        } else
        {
            record_success(get_estimator(pending->path), completion_slack_ns);
        }
    }

    pending_frames.erase(pending);
}

void adaptive_repaint_scheduler_t::reset(bool reset_estimates)
{
    last_presentation_ns.reset();
    refresh_ns = 0;
    pending_frames.clear();
    consecutive_scanouts = 0;
    last_path = repaint_path_t::COMPOSED;
    if (reset_estimates)
    {
        composed = {};
        scanout  = {};
    }
}
}
