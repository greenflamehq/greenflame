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
