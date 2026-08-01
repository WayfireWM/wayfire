#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "output/adaptive-repaint-scheduler.hpp"

namespace
{
constexpr int64_t MS = 1'000'000;
constexpr int64_t PERIOD_60HZ = 16'666'667;

wf::repaint_presentation_t present(uint32_t seq, int64_t when,
    int64_t refresh = PERIOD_60HZ)
{
    return {seq, true, when, refresh, true};
}

int64_t delay_ms(const wf::repaint_schedule_t& schedule)
{
    return schedule.delay_ns / MS;
}
}

TEST_CASE("Repaint scheduler starts conservatively and uses presentation deadlines")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    CHECK(delay_ms(scheduler.schedule_frame(0, 5, true, false)) == 0);

    scheduler.handle_presentation(present(1, 100 * MS));
    // Dynamic mode has no duration sample yet, so the first frame remains conservative.
    CHECK(delay_ms(scheduler.schedule_frame(100 * MS, 5, true, false)) == 0);

    auto first = scheduler.schedule_frame(100 * MS, 5, false, false);
    CHECK(delay_ms(first) == 11);
    scheduler.submit_frame(first, wf::repaint_path_t::COMPOSED,
        111 * MS, 113 * MS, 2, false);
    scheduler.handle_presentation(present(2, 100 * MS + PERIOD_60HZ));

    auto learned = scheduler.schedule_frame(100 * MS + PERIOD_60HZ, 1, true, false);
    CHECK(delay_ms(learned) >= 13);
    CHECK(delay_ms(learned) <= 14);
}

TEST_CASE("Dynamic repaint treats a disabled minimum budget as a zero floor")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));

    CHECK_FALSE(scheduler.schedule_frame(100 * MS, -1, false, false).
        target_presentation_ns.has_value());

    auto first = scheduler.schedule_frame(100 * MS, -1, true, false);
    CHECK(first.target_presentation_ns.has_value());
    CHECK(first.delay_ns == 0);
    scheduler.submit_frame(first, wf::repaint_path_t::COMPOSED,
        100 * MS, 102 * MS, 2, false);
    scheduler.handle_presentation(present(2, first.target_presentation_ns.value()));

    auto learned = scheduler.schedule_frame(first.target_presentation_ns.value(), -1, true, false);
    CHECK(learned.delay_ns > 0);
}

TEST_CASE("Repaint scheduler correlates misses by commit sequence")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    auto frame = scheduler.schedule_frame(100 * MS, 1, false, false);
    scheduler.submit_frame(frame, wf::repaint_path_t::COMPOSED,
        110 * MS, 111 * MS, 2, false);

    const auto before = scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1);
    scheduler.handle_presentation(present(99, 100 * MS + PERIOD_60HZ));
    CHECK(scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1) == before);

    scheduler.handle_presentation(present(2, 100 * MS + 2 * PERIOD_60HZ));
    CHECK(scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1) > before);
}

TEST_CASE("Fixed-refresh scheduling preserves phase across sparse updates")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));

    auto first = scheduler.schedule_frame(100 * MS, 0, true, false);
    scheduler.submit_frame(first, wf::repaint_path_t::COMPOSED,
        100 * MS, 102 * MS, 2, false);
    scheduler.handle_presentation(present(2, first.target_presentation_ns.value()));

    const int64_t now = first.target_presentation_ns.value() + 205 * MS;
    auto sparse = scheduler.schedule_frame(now, 0, true, false);
    CHECK(sparse.target_presentation_ns ==
        first.target_presentation_ns.value() + 13 * PERIOD_60HZ);
    CHECK(delay_ms(sparse) >= 8);
    CHECK(delay_ms(sparse) <= 9);
}

TEST_CASE("Fixed-refresh scheduling advances past an elapsed nominal slot")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));

    const int64_t now = 100 * MS + PERIOD_60HZ + MS;
    auto schedule     = scheduler.schedule_frame(now, 1, false, false);
    CHECK(schedule.target_presentation_ns == 100 * MS + 2 * PERIOD_60HZ);
    CHECK(delay_ms(schedule) >= 14);
    CHECK(schedule.delay_ns == 14'666'667);
    CHECK(schedule.repaint_deadline_ns == schedule.target_presentation_ns.value() - MS);
}

TEST_CASE("Small future presentation anchors preserve fixed-refresh phase")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));

    auto schedule = scheduler.schedule_frame(100 * MS - 200'000, 5, false, false);
    CHECK(schedule.presentation_anchor_age_ns == -200'000);
    CHECK(schedule.presentation_anchor_clamped);
    CHECK(schedule.target_presentation_ns == 100 * MS + PERIOD_60HZ);
    CHECK(schedule.repaint_deadline_ns == 100 * MS + PERIOD_60HZ - 5 * MS);
    CHECK(schedule.delay_ns == 11'866'667);
}

TEST_CASE("Excessive future presentation anchors disable delayed repaint")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));

    auto schedule = scheduler.schedule_frame(100 * MS - 1'000'001, 5, false, false);
    CHECK(schedule.presentation_anchor_age_ns == -1'000'001);
    CHECK_FALSE(schedule.presentation_anchor_clamped);
    CHECK_FALSE(schedule.target_presentation_ns.has_value());
    CHECK(schedule.repaint_deadline_ns == 0);
    CHECK(schedule.delay_ns == 0);
}

TEST_CASE("A missed vblank adds a bounded guard")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    auto frame = scheduler.schedule_frame(100 * MS, 0, false, false);
    scheduler.submit_frame(frame, wf::repaint_path_t::COMPOSED,
        100 * MS, 102 * MS, 2, false);

    scheduler.handle_presentation(present(2,
        frame.target_presentation_ns.value() + PERIOD_60HZ));
    auto info = scheduler.get_debug_info(0);
    CHECK(info.composed.miss_guard_ns == 500'000);
    CHECK(info.composed.total_budget_ns < PERIOD_60HZ);
    CHECK(scheduler.schedule_frame(
        frame.target_presentation_ns.value() + PERIOD_60HZ,
        0, true, false).delay_ns > 0);
}

TEST_CASE("A render-late miss raises paint without double-counting guard")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    auto frame = scheduler.schedule_frame(100 * MS, 0, false, false);
    const int64_t target = frame.target_presentation_ns.value();
    scheduler.submit_frame(frame, wf::repaint_path_t::COMPOSED,
        target - 2 * MS, target - MS, 2, false);
    scheduler.observe_render_completion(2, wf::repaint_path_t::COMPOSED, 3 * MS);

    scheduler.handle_presentation(present(2, target + PERIOD_60HZ));
    auto info = scheduler.get_debug_info(0);
    CHECK(info.composed.paint_budget_ns == 3 * MS + 250'000);
    CHECK(info.composed.miss_guard_ns == 0);
}

TEST_CASE("A latch-late miss raises guard after rendering completed")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    auto frame = scheduler.schedule_frame(100 * MS, 0, false, false);
    const int64_t target = frame.target_presentation_ns.value();
    scheduler.submit_frame(frame, wf::repaint_path_t::COMPOSED,
        target - 3 * MS, target - 2 * MS, 2, false);
    scheduler.observe_render_completion(2, wf::repaint_path_t::COMPOSED, MS);

    scheduler.handle_presentation(present(2, target + PERIOD_60HZ));
    CHECK(scheduler.get_debug_info(0).composed.miss_guard_ns == 500'000);
}

TEST_CASE("Successful frames probe the guard downward despite completion slack")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    auto missed    = scheduler.schedule_frame(100 * MS, 0, false, false);
    int64_t target = missed.target_presentation_ns.value();
    scheduler.submit_frame(missed, wf::repaint_path_t::COMPOSED,
        target - 3 * MS, target - 2 * MS, 2, false);
    scheduler.observe_render_completion(2, wf::repaint_path_t::COMPOSED, MS);
    scheduler.handle_presentation(present(2, target + PERIOD_60HZ));
    CHECK(scheduler.get_debug_info(0).composed.miss_guard_ns == 500'000);

    auto enough_slack = scheduler.schedule_frame(target + PERIOD_60HZ, 0, true, false);
    target = enough_slack.target_presentation_ns.value();
    scheduler.submit_frame(enough_slack, wf::repaint_path_t::COMPOSED,
        target - 1'700'000, target - 1'500'000, 3, false);
    scheduler.observe_render_completion(3, wf::repaint_path_t::COMPOSED, MS);
    scheduler.handle_presentation(present(3, target));
    CHECK(scheduler.get_debug_info(0).composed.miss_guard_ns == 450'000);

    auto tighter_slack = scheduler.schedule_frame(target, 0, true, false);
    target = tighter_slack.target_presentation_ns.value();
    scheduler.submit_frame(tighter_slack, wf::repaint_path_t::COMPOSED,
        target - 1'300'000, target - MS, 4, false);
    scheduler.observe_render_completion(4, wf::repaint_path_t::COMPOSED, MS);
    scheduler.handle_presentation(present(4, target));
    CHECK(scheduler.get_debug_info(0).composed.miss_guard_ns == 400'000);
}

TEST_CASE("Successful sparse frames recover the guard despite duration changes")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    auto missed = scheduler.schedule_frame(100 * MS, 0, false, false);
    scheduler.submit_frame(missed, wf::repaint_path_t::COMPOSED,
        100 * MS, 100 * MS + 500'000, 2, false);
    int64_t presented_at = missed.target_presentation_ns.value() + PERIOD_60HZ;
    scheduler.handle_presentation(present(2, presented_at));

    for (uint32_t seq = 3; seq <= 12; seq++)
    {
        const int64_t now = presented_at + 200 * MS + seq * 100'000;
        auto frame = scheduler.schedule_frame(now, 0, true, false);
        CHECK(frame.target_presentation_ns.has_value());
        scheduler.submit_frame(frame, wf::repaint_path_t::COMPOSED,
            now, now + seq * 100'000, seq, false);
        presented_at = frame.target_presentation_ns.value();
        scheduler.handle_presentation(present(seq, presented_at));
    }

    auto info = scheduler.get_debug_info(0);
    CHECK(info.composed.miss_guard_ns == 0);
    CHECK(info.composed.successful_presentations == 10);
}

TEST_CASE("Discarded frames do not increase the guard")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    auto frame = scheduler.schedule_frame(100 * MS, 0, false, false);
    scheduler.submit_frame(frame, wf::repaint_path_t::COMPOSED,
        100 * MS, 102 * MS, 2, false);

    scheduler.handle_presentation({2, false, 0, 0, true});
    auto info = scheduler.get_debug_info(0);
    CHECK(info.composed.miss_guard_ns == 0);
    CHECK(info.pending_frames == 0);
    CHECK_FALSE(info.has_last_presentation);
}

TEST_CASE("Repaint scheduler learns scanout separately")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));

    for (uint32_t seq = 2; seq <= 4; seq++)
    {
        auto frame = scheduler.schedule_frame(100 * MS, 1, true, false);
        scheduler.submit_frame(frame, wf::repaint_path_t::DIRECT_SCANOUT,
            100 * MS, 100 * MS + 100'000, seq, false);
    }

    auto frame = scheduler.schedule_frame(100 * MS, 1, true, false);
    CHECK(frame.predicted_path == wf::repaint_path_t::DIRECT_SCANOUT);
    CHECK(scheduler.get_budget_ns(wf::repaint_path_t::DIRECT_SCANOUT, 1) <
        scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1));

    scheduler.submit_frame(frame, wf::repaint_path_t::COMPOSED,
        100 * MS, 103 * MS, 5, false);
    CHECK(scheduler.schedule_frame(100 * MS, 1, true, false).predicted_path ==
        wf::repaint_path_t::COMPOSED);
}

TEST_CASE("VRR pacing does not classify variable intervals as misses")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS, 7 * MS));
    auto frame = scheduler.schedule_frame(100 * MS, 1, false, true);
    CHECK(delay_ms(frame) == 6);
    scheduler.submit_frame(frame, wf::repaint_path_t::COMPOSED,
        105 * MS, 106 * MS, 2, true);
    const auto before = scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1);

    scheduler.handle_presentation(present(2, 125 * MS, 7 * MS));
    CHECK(scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1) == before);
    // The old anchor is stale compared with the nominal fastest period.
    CHECK(delay_ms(scheduler.schedule_frame(150 * MS, 1, false, true)) == 0);
}

TEST_CASE("Invalid presentation data disables delayed repaint")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    CHECK(scheduler.schedule_frame(100 * MS, 5, false, false).delay_ns > 0);

    scheduler.handle_presentation({2, false, 0, 0});
    CHECK(delay_ms(scheduler.schedule_frame(100 * MS, 5, false, false)) == 0);

    scheduler.handle_presentation({3, true, 120 * MS, 0});
    CHECK(delay_ms(scheduler.schedule_frame(120 * MS, 5, false, false)) == 0);
}

TEST_CASE("Unreliable presentation timestamps do not tune the deadline guard")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    auto frame = scheduler.schedule_frame(100 * MS, 1, false, false);
    scheduler.submit_frame(frame, wf::repaint_path_t::COMPOSED,
        110 * MS, 111 * MS, 2, false);
    const auto before = scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1);

    scheduler.handle_presentation({2, true, 100 * MS + 2 * PERIOD_60HZ,
        PERIOD_60HZ, false});
    CHECK(scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1) == before);
}

TEST_CASE("Reset and disabled delay return to conservative scheduling")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    CHECK(delay_ms(scheduler.schedule_frame(100 * MS, -1, true, false)) == 0);
    CHECK(scheduler.schedule_frame(100 * MS, 5, false, false).delay_ns > 0);

    scheduler.reset();
    CHECK(delay_ms(scheduler.schedule_frame(100 * MS, 5, false, false)) == 0);
}

TEST_CASE("Render completion raises but does not prematurely lower the learned budget")
{
    wf::adaptive_repaint_scheduler_t scheduler;
    scheduler.handle_presentation(present(1, 100 * MS));
    auto frame = scheduler.schedule_frame(100 * MS, 1, true, false);
    scheduler.submit_frame(frame, wf::repaint_path_t::COMPOSED,
        100 * MS, 102 * MS, 2, false);

    const auto cpu_budget = scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1);
    scheduler.observe_render_completion(2, wf::repaint_path_t::COMPOSED, 6 * MS);
    const auto gpu_budget = scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1);
    CHECK(gpu_budget > cpu_budget);
    CHECK(gpu_budget == 6 * MS + 250'000);

    scheduler.observe_render_completion(2, wf::repaint_path_t::COMPOSED, 1 * MS);
    CHECK(scheduler.get_budget_ns(wf::repaint_path_t::COMPOSED, 1) == gpu_budget);
}
