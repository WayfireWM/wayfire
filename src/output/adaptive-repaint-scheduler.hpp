#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <wayfire/render-manager.hpp>

namespace wf
{
enum class repaint_path_t
{
    COMPOSED,
    DIRECT_SCANOUT,
};

struct repaint_schedule_t
{
    /** Predicted presentation timestamp in CLOCK_MONOTONIC nanoseconds. */
    std::optional<int64_t> target_presentation_ns;
    /** Render path whose learned budget was used for this schedule. */
    repaint_path_t predicted_path = repaint_path_t::COMPOSED;
    /** Absolute CLOCK_MONOTONIC render start time, or zero for immediate rendering. */
    int64_t repaint_deadline_ns = 0;
    /** Time from scheduling until repaint_deadline_ns, in nanoseconds. */
    int64_t delay_ns = 0;
    /** Raw scheduling time minus the presentation anchor, in nanoseconds. */
    int64_t presentation_anchor_age_ns = 0;
    /** Whether a small future hardware timestamp was clamped to age zero. */
    bool presentation_anchor_clamped = false;
};

struct repaint_presentation_t
{
    /** wlroots output commit sequence used to correlate a submitted frame. */
    uint32_t commit_seq = 0;
    /** Whether the backend presented, rather than discarded, the commit. */
    bool presented = false;
    /** Actual CLOCK_MONOTONIC presentation timestamp, in nanoseconds. */
    int64_t when_ns = 0;
    /** Nominal refresh period reported by the backend, in nanoseconds. */
    int64_t refresh_ns = 0;
    /** Whether when_ns comes from hardware clock and completion feedback. */
    bool timing_reliable = false;
};

struct repaint_scheduler_debug_info_t
{
    /** Whether last_presentation_ns contains a usable presentation anchor. */
    bool has_last_presentation = false;
    /** Most recent usable CLOCK_MONOTONIC presentation timestamp. */
    int64_t last_presentation_ns = 0;
    /** Most recently reported nominal refresh period, in nanoseconds. */
    int64_t refresh_ns = 0;
    /** Submitted commits waiting for correlated presentation feedback. */
    uint32_t pending_frames = 0;
    /** Number of consecutive direct-scanout presentations. */
    uint32_t consecutive_scanouts = 0;
    /** Path whose budget would be used for the next repaint. */
    repaint_path_t predicted_path = repaint_path_t::COMPOSED;
    render_path_debug_info_t composed;
    render_path_debug_info_t direct_scanout;
};

/**
 * Adaptive repaint scheduler which learns the time required to paint and submit a frame. The information is
 * then used to delay the start of rendering after a vblank event arrives, allowing the compositor to wait
 * for the latest possible moment to start rendering without missing frames. This grace period can give
 * clients time to render and update their buffers, which can then be immediately presented on the next frame.
 * Overall, this results in a lower latency from input to presentation.
 *
 * The scheduler keeps two separate estimates, one for the composed (normal rendering) path and one for the
 * direct scanout path, as these have very different performance characteristics.
 *
 * In order to estimate the time the compositor can delay painting a frame, the scheduler keeps track of
 * a paint budget and a miss guard. The paint budget is the estimated time it takes to paint and submit a
 * frame, while the miss guard measures a safety margin which models CPU context switching and other sources
 * of delays, which further constrains the time the compositor can wait.
 */
class adaptive_repaint_scheduler_t
{
  public:
    /**
     * Compute the exact repaint deadline and expected render path for a frame.
     * The values are based on the estimated paint budget and miss guard, as well as the configured render
     * budget.
     *
     * @param now_ns Current CLOCK_MONOTONIC time in nanoseconds.
     * @param min_render_budget_ms Configured minimum render budget in milliseconds, or -1 to ignore
     * @param dynamic_delay Whether the compositor is allowed to delay rendering to wait for client updates.
     * @param vrr_enabled Whether the output is currently using variable refresh rate.
     */
    repaint_schedule_t schedule_frame(int64_t now_ns, int min_render_budget_ms,
        bool dynamic_delay, bool vrr_enabled) const;

    /** Record CPU render duration and state used to correlate presentation feedback. */
    void submit_frame(const repaint_schedule_t& schedule, repaint_path_t path,
        int64_t paint_started_ns, int64_t committed_ns, uint32_t commit_seq,
        bool vrr_enabled);

    /** Update paint budget based on the observed render duration with wlr_render_timer. */
    void observe_render_completion(uint32_t commit_seq, repaint_path_t path, int64_t duration_ns);
    /** Update presentation phase and tune the miss guard from correlated feedback. */
    void handle_presentation(const repaint_presentation_t& event);
    /** Drop presentation state and optionally the learned path estimates. */
    void reset(bool reset_estimates = false);

    /** Return the effective path budget in nanoseconds; min_render_budget_ms is a floor. */
    int64_t get_budget_ns(repaint_path_t path, int min_render_budget_ms) const;
    /** Return a read-only snapshot of scheduler state for diagnostics. */
    repaint_scheduler_debug_info_t get_debug_info(int min_render_budget_ms) const;

  private:
    struct estimator_t
    {
        int64_t paint_budget_ns = 0;
        int64_t miss_guard_ns   = 0;
        uint32_t successful_presentations = 0;
    };

    struct pending_frame_t
    {
        uint32_t commit_seq;
        repaint_path_t path;
        std::optional<int64_t> target_presentation_ns;
        std::optional<int64_t> render_completion_ns;
        int64_t paint_started_ns;
        int64_t refresh_ns;
        bool vrr_enabled;
    };

    estimator_t& get_estimator(repaint_path_t path);
    const estimator_t& get_estimator(repaint_path_t path) const;
    repaint_path_t predict_path() const;
    void update_duration(estimator_t& estimator, int64_t duration_ns);
    void raise_duration_estimate(estimator_t& estimator, int64_t duration_ns);
    void record_miss(estimator_t& estimator, int64_t refresh_ns);
    void record_success(estimator_t& estimator,
        std::optional<int64_t> completion_slack_ns);

    std::optional<int64_t> last_presentation_ns;
    int64_t refresh_ns = 0;
    estimator_t composed;
    estimator_t scanout;
    std::deque<pending_frame_t> pending_frames;
    repaint_path_t last_path = repaint_path_t::COMPOSED;
    uint32_t consecutive_scanouts = 0;
};
}
