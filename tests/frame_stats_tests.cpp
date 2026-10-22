#include "greenflame_core/frame_stats.h"

using namespace greenflame::core;

namespace {

constexpr double kJustOver = 0.001;

std::vector<double> One_to_hundred() {
    std::vector<double> values;
    for (int i = 1; i <= 100; ++i) {
        values.push_back(static_cast<double>(i));
    }
    return values;
}

} // namespace

TEST(frame_stats, EmptyInput_GivesZeroCountAndFail) {
    FrameTimeSummary const summary = Summarize_frame_times({});
    EXPECT_EQ(summary.count, 0u);
    EXPECT_EQ(summary.p95_ms, 0.0);
    EXPECT_EQ(Judge_frame_times(summary), FrameRateVerdict::Fail);
}

TEST(frame_stats, OneSample_EveryPercentileIsThatSample) {
    std::array<double, 1> const values = {{12.5}};
    FrameTimeSummary const summary = Summarize_frame_times(values);
    EXPECT_EQ(summary.count, 1u);
    EXPECT_EQ(summary.p50_ms, 12.5);
    EXPECT_EQ(summary.p99_ms, 12.5);
    EXPECT_EQ(summary.max_ms, 12.5);
    EXPECT_EQ(summary.percent_within_60fps, 100.0);
}

TEST(frame_stats, HundredSamples_NearestRankPercentiles) {
    std::vector<double> const values = One_to_hundred();
    FrameTimeSummary const summary = Summarize_frame_times(values);
    EXPECT_EQ(summary.count, 100u);
    EXPECT_EQ(summary.p50_ms, 50.0);
    EXPECT_EQ(summary.p90_ms, 90.0);
    EXPECT_EQ(summary.p95_ms, 95.0);
    EXPECT_EQ(summary.p99_ms, 99.0);
    EXPECT_EQ(summary.max_ms, 100.0);
    // 1..33 are within 33.3 ms, 1..16 within 16.7 ms.
    EXPECT_DOUBLE_EQ(summary.percent_within_30fps, 33.0);
    EXPECT_DOUBLE_EQ(summary.percent_within_60fps, 16.0);
}

TEST(frame_stats, TenSamples_FractionalRanksRoundUp) {
    std::vector<double> values;
    for (int i = 1; i <= 10; ++i) {
        values.push_back(static_cast<double>(i));
    }
    FrameTimeSummary const summary = Summarize_frame_times(values);
    // Ranks 9.5 and 9.9 round up to the 10th sample; rank 5 is the 5th.
    EXPECT_EQ(summary.p95_ms, 10.0);
    EXPECT_EQ(summary.p99_ms, 10.0);
    EXPECT_EQ(summary.p90_ms, 9.0);
    EXPECT_EQ(summary.p50_ms, 5.0);
}

TEST(frame_stats, UnsortedInput_SameAsSorted) {
    std::vector<double> values = One_to_hundred();
    std::reverse(values.begin(), values.end());
    std::swap(values[3], values[70]);
    FrameTimeSummary const summary = Summarize_frame_times(values);
    EXPECT_EQ(summary.p50_ms, 50.0);
    EXPECT_EQ(summary.p95_ms, 95.0);
    EXPECT_EQ(summary.max_ms, 100.0);
}

TEST(frame_stats, Verdict_AtAndJustOverEachBudget) {
    FrameTimeSummary summary{};
    summary.count = 1;
    summary.p95_ms = kFrameBudget60Ms;
    EXPECT_EQ(Judge_frame_times(summary), FrameRateVerdict::Pass60);
    summary.p95_ms = kFrameBudget60Ms + kJustOver;
    EXPECT_EQ(Judge_frame_times(summary), FrameRateVerdict::Pass30);
    summary.p95_ms = kFrameBudget30Ms;
    EXPECT_EQ(Judge_frame_times(summary), FrameRateVerdict::Pass30);
    summary.p95_ms = kFrameBudget30Ms + kJustOver;
    EXPECT_EQ(Judge_frame_times(summary), FrameRateVerdict::Fail);
}

TEST(frame_stats, VerdictLabels) {
    EXPECT_EQ(Frame_rate_verdict_label(FrameRateVerdict::Pass60), "PASS-60");
    EXPECT_EQ(Frame_rate_verdict_label(FrameRateVerdict::Pass30), "PASS-30");
    EXPECT_EQ(Frame_rate_verdict_label(FrameRateVerdict::Fail), "FAIL");
}

TEST(frame_stats, Spike_FollowsItsInputPromptly_IsIdle) {
    FrameIntervalParts const parts{.interval_ms = 120.0,
                                   .input_to_end_ms = kFrameSpikeMs,
                                   .wait_ms = 45.0,
                                   .paint_ms = 45.0};
    EXPECT_EQ(Classify_frame_spike(parts), FrameSpikeKind::Idle);
}

TEST(frame_stats, Spike_WaitLargestPart_IsWait) {
    FrameIntervalParts const parts{.interval_ms = 120.0,
                                   .input_to_end_ms = 110.0,
                                   .wait_ms = 60.0,
                                   .paint_ms = 45.0};
    EXPECT_EQ(Classify_frame_spike(parts), FrameSpikeKind::Wait);
}

TEST(frame_stats, Spike_PaintLargestPart_IsPaint) {
    FrameIntervalParts const parts{.interval_ms = 120.0,
                                   .input_to_end_ms = 110.0,
                                   .wait_ms = 41.0,
                                   .paint_ms = 45.0};
    EXPECT_EQ(Classify_frame_spike(parts), FrameSpikeKind::Paint);
}

TEST(frame_stats, Spike_LoopDelayLargestPart_IsLate) {
    FrameIntervalParts const parts{.interval_ms = 120.0,
                                   .input_to_end_ms = kFrameSpikeMs + 1.0,
                                   .wait_ms = 10.0,
                                   .paint_ms = 10.0};
    EXPECT_EQ(Classify_frame_spike(parts), FrameSpikeKind::Late);
}

// Wait and paint split the spike evenly with no loop delay: the tie goes to wait,
// not "late".
TEST(frame_stats, Spike_EvenWaitAndPaint_IsWaitNotLate) {
    FrameIntervalParts const parts{.interval_ms = 70.0,
                                   .input_to_end_ms = 70.0,
                                   .wait_ms = 35.0,
                                   .paint_ms = 35.0};
    EXPECT_EQ(Classify_frame_spike(parts), FrameSpikeKind::Wait);
}

// The loop delay is the largest part, so a long wait does not explain the spike.
TEST(frame_stats, Spike_LongLoopDelayWithLongWait_IsLate) {
    FrameIntervalParts const parts{.interval_ms = 200.0,
                                   .input_to_end_ms = 200.0,
                                   .wait_ms = 41.0,
                                   .paint_ms = 5.0};
    EXPECT_EQ(Classify_frame_spike(parts), FrameSpikeKind::Late);
}

TEST(frame_stats, SpikeSummary_CountsOnlyLongIntervals) {
    std::vector<FrameIntervalParts> const frames = {
        {.interval_ms = kFrameSpikeMs, .input_to_end_ms = 200.0, .wait_ms = 45.0},
        {.interval_ms = 60.0, .input_to_end_ms = 20.0},
        {.interval_ms = 150.0, .input_to_end_ms = 30.0},
        {.interval_ms = 90.0, .input_to_end_ms = 90.0, .wait_ms = 80.0},
    };
    auto const counts = Summarize_frame_spikes(frames).by_kind;
    FrameSpikeCount const idle = counts[static_cast<size_t>(FrameSpikeKind::Idle)];
    FrameSpikeCount const wait = counts[static_cast<size_t>(FrameSpikeKind::Wait)];
    EXPECT_EQ(idle.count, 2u);
    EXPECT_EQ(idle.max_ms, 150.0);
    EXPECT_EQ(wait.count, 1u);
    EXPECT_EQ(wait.max_ms, 90.0);
    EXPECT_EQ(counts[static_cast<size_t>(FrameSpikeKind::Paint)].count, 0u);
    EXPECT_EQ(counts[static_cast<size_t>(FrameSpikeKind::Late)].count, 0u);
}

TEST(frame_stats, SpikeKindLabels) {
    EXPECT_EQ(Frame_spike_kind_label(FrameSpikeKind::Idle), "idle");
    EXPECT_EQ(Frame_spike_kind_label(FrameSpikeKind::Wait), "wait");
    EXPECT_EQ(Frame_spike_kind_label(FrameSpikeKind::Paint), "paint");
    EXPECT_EQ(Frame_spike_kind_label(FrameSpikeKind::Late), "late");
}
