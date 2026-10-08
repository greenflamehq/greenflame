#include "greenflame_core/freehand_smoothing.h"

using namespace greenflame::core;

// Opt-in CPU benchmark; timings are reported, never used as pass/fail thresholds.
TEST(freehand_smoothing, DISABLED_LongStrokePerformance) {
    constexpr int32_t point_count = 8192;
    constexpr int32_t row_width = 512;
    constexpr int32_t point_spacing_px = 3;
    constexpr int32_t wave_height_px = 16;
    constexpr int32_t wave_period = 32;
    constexpr int32_t stroke_width_px = 2;
    constexpr int32_t iterations = 100;
    std::vector<PointPx> points;
    points.reserve(point_count);
    for (int32_t index = 0; index < point_count; ++index) {
        points.push_back({(index % row_width) * point_spacing_px,
                          (index / row_width) * wave_height_px +
                              static_cast<int32_t>(
                                  wave_height_px *
                                  std::sin(static_cast<double>(index) / wave_period))});
    }
    size_t output_point_count = 0;
    auto const start = std::chrono::steady_clock::now();
    for (int32_t iteration = 0; iteration < iterations; ++iteration) {
        auto const smoothed = Smooth_freehand_points(
            points, FreehandSmoothingMode::Smooth, stroke_width_px);
        output_point_count += smoothed.size();
    }
    auto const elapsed = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - start);
    std::cout << "Smoothing " << point_count
              << " points: " << elapsed.count() / iterations
              << " us/call; output points: " << output_point_count / iterations << '\n';
    EXPECT_GT(output_point_count, points.size());

    // The live preview appends points one at a time. Its cost per append must not
    // grow with the stroke: compare the first and the last 1024 appends.
    constexpr size_t window = 1024;
    std::array<double, 2> window_us = {};
    for (int32_t iteration = 0; iteration < iterations; ++iteration) {
        FreehandIncrementalSmoother smoother;
        smoother.Reset(FreehandSmoothingMode::Smooth, stroke_width_px);
        for (size_t index = 0; index < points.size(); ++index) {
            bool const timed = index < window || index >= points.size() - window;
            auto const append_start = std::chrono::steady_clock::now();
            smoother.Append(std::span<const PointPx>(points).subspan(index, 1));
            if (timed) {
                window_us[index < window ? 0 : 1] +=
                    std::chrono::duration<double, std::micro>(
                        std::chrono::steady_clock::now() - append_start)
                        .count();
            }
        }
    }
    double const per_append = static_cast<double>(window) * iterations;
    std::cout << "Incremental append: first " << window
              << " points: " << window_us[0] / per_append << " us/append; last "
              << window << " points: " << window_us[1] / per_append << " us/append\n";
}

TEST(freehand_smoothing, SmoothMode_GoldenCurve) {
    std::vector<PointPx> const points = {{-8, -4}, {-3, -3}, {0, 0}, {4, 2},
                                         {4, 2},   {8, 1},   {12, 5}};
    auto const smoothed =
        Smooth_freehand_points(points, FreehandSmoothingMode::Smooth, 8);
    std::vector<PointPx> const expected = {
        {-8, -4}, {-6, -4}, {-5, -4}, {-3, -3}, {-2, -2}, {-1, -1}, {0, 0},  {1, 1},
        {3, 2},   {4, 2},   {5, 2},   {7, 1},   {8, 1},   {9, 2},   {11, 3}, {12, 5}};
    EXPECT_EQ(smoothed, expected);
}

TEST(freehand_smoothing, SmoothMode_DeduplicatesStationaryAndJoinedSubpaths) {
    constexpr int32_t stroke_width_px = 8;
    std::vector<PointPx> const stationary = {{0, 0}, {0, 0}, {0, 0}};
    EXPECT_EQ(Smooth_freehand_points(stationary, FreehandSmoothingMode::Smooth,
                                     stroke_width_px),
              (std::vector<PointPx>{{0, 0}}));

    std::vector<PointPx> const corners = {{-4, 0}, {0, 0}, {0, 0},
                                          {4, 0},  {0, 0}, {0, 4}};
    std::vector<PointPx> const expected = {{-4, 0}, {-2, 0}, {0, 0}, {2, 0},
                                           {4, 0},  {0, 0},  {0, 4}};
    EXPECT_EQ(
        Smooth_freehand_points(corners, FreehandSmoothingMode::Smooth, stroke_width_px),
        expected);
}

TEST(freehand_smoothing, OffMode_PreservesInputExactly) {
    std::vector<PointPx> const points = {{10, 10}, {20, 11}, {30, 13}, {30, 13}};

    EXPECT_EQ(Smooth_freehand_points(points, FreehandSmoothingMode::Off, 6), points);
}

TEST(freehand_smoothing, SmoothMode_PreservesEndpointsAndSharpCorners) {
    std::vector<PointPx> const points = {
        {10, 10}, {20, 10}, {30, 10}, {30, 20}, {30, 30}};

    std::vector<PointPx> const smoothed =
        Smooth_freehand_points(points, FreehandSmoothingMode::Smooth, 6);

    ASSERT_GE(smoothed.size(), 3u);
    EXPECT_EQ(smoothed.front(), points.front());
    EXPECT_EQ(smoothed.back(), points.back());
    EXPECT_NE(std::find(smoothed.begin(), smoothed.end(), PointPx{30, 10}),
              smoothed.end());
}

TEST(freehand_smoothing, SmoothMode_ResamplesGentleCurves) {
    std::vector<PointPx> const points = {{10, 10}, {20, 10}, {30, 20}, {40, 20}};

    std::vector<PointPx> const smoothed =
        Smooth_freehand_points(points, FreehandSmoothingMode::Smooth, 6);

    ASSERT_GT(smoothed.size(), points.size());
    EXPECT_EQ(smoothed.front(), points.front());
    EXPECT_EQ(smoothed.back(), points.back());
}

TEST(freehand_smoothing, PreviewSplit_KeepsAllRawPointsWhenModeIsOff) {
    std::vector<PointPx> const points = {{10, 10}, {20, 10}, {30, 20}, {40, 20}};

    FreehandPreviewSegments const preview =
        Build_freehand_preview_segments(points, FreehandSmoothingMode::Off, 8);

    EXPECT_TRUE(preview.stable_points.empty());
    EXPECT_EQ(preview.tail_points, points);
}

TEST(freehand_smoothing, PreviewSplit_SmoothsStableBodyAndKeepsRawTail) {
    std::vector<PointPx> const points = {{10, 10}, {20, 10}, {30, 15}, {40, 20},
                                         {50, 25}, {60, 30}, {70, 30}, {80, 30},
                                         {90, 30}, {100, 30}};

    FreehandPreviewSegments const preview =
        Build_freehand_preview_segments(points, FreehandSmoothingMode::Smooth, 6);

    ASSERT_FALSE(preview.stable_points.empty());
    ASSERT_FALSE(preview.tail_points.empty());
    EXPECT_EQ(preview.stable_points.front(), points.front());
    EXPECT_EQ(preview.tail_points.back(), points.back());
    EXPECT_LT(preview.tail_points.size(), points.size());
}

TEST(freehand_smoothing, PreviewPlan_ReportsStablePrefixAndTailStartIndex) {
    std::vector<PointPx> const points = {{10, 10}, {20, 10}, {30, 15}, {40, 20},
                                         {50, 25}, {60, 30}, {70, 30}, {80, 30},
                                         {90, 30}, {100, 30}};

    FreehandPreviewPlan const plan =
        Build_freehand_preview_plan(points, FreehandSmoothingMode::Smooth, 6);

    ASSERT_GT(plan.stable_raw_point_count, 0u);
    EXPECT_EQ(plan.stable_raw_point_count, plan.tail_start_index + 1);
    ASSERT_FALSE(plan.tail_points.empty());
    EXPECT_EQ(plan.tail_points.front(), points[plan.tail_start_index]);
    EXPECT_EQ(plan.tail_points.back(), points.back());
}

TEST(freehand_smoothing, PreviewPlan_ExtendedStrokeKeepsSmoothedStablePrefix) {
    std::vector<PointPx> const points_before = {{10, 10}, {20, 10}, {30, 15}, {40, 20},
                                                {50, 25}, {60, 30}, {70, 30}, {80, 30},
                                                {90, 30}, {100, 30}};
    std::vector<PointPx> const points_after = {
        {10, 10},  {20, 10},  {30, 15},  {40, 20},  {50, 25},  {60, 30},
        {70, 30},  {80, 30},  {90, 30},  {100, 30}, {110, 32}, {120, 35},
        {130, 39}, {140, 42}, {150, 44}, {160, 45}};

    FreehandPreviewPlan const plan_before =
        Build_freehand_preview_plan(points_before, FreehandSmoothingMode::Smooth, 6);
    FreehandPreviewPlan const plan_after =
        Build_freehand_preview_plan(points_after, FreehandSmoothingMode::Smooth, 6);

    ASSERT_GT(plan_before.stable_raw_point_count, 0u);
    ASSERT_GT(plan_after.stable_raw_point_count, plan_before.stable_raw_point_count);

    std::vector<PointPx> const smoothed_before =
        Smooth_freehand_points(std::span<const PointPx>(points_before)
                                   .first(plan_before.stable_raw_point_count),
                               FreehandSmoothingMode::Smooth, 6);
    std::vector<PointPx> const smoothed_after = Smooth_freehand_points(
        std::span<const PointPx>(points_after).first(plan_after.stable_raw_point_count),
        FreehandSmoothingMode::Smooth, 6);

    ASSERT_GE(smoothed_after.size(), smoothed_before.size());
    EXPECT_TRUE(std::equal(smoothed_before.begin(), smoothed_before.end(),
                           smoothed_after.begin()));
}

namespace {

[[nodiscard]] std::vector<PointPx> Spiral_stroke() {
    constexpr int32_t point_count = 600;
    constexpr double radians_per_point = 0.09;
    constexpr double radius_growth_px = 0.25;
    constexpr double base_radius_px = 30.0;
    std::vector<PointPx> points;
    for (int32_t index = 0; index < point_count; ++index) {
        double const angle = static_cast<double>(index) * radians_per_point;
        double const radius = base_radius_px + radius_growth_px * index;
        points.push_back({static_cast<int32_t>(std::lround(radius * std::cos(angle))),
                          static_cast<int32_t>(std::lround(radius * std::sin(angle)))});
    }
    return points;
}

[[nodiscard]] std::vector<PointPx> Zigzag_stroke() {
    constexpr int32_t teeth = 40;
    constexpr int32_t tooth_px = 20;
    std::vector<PointPx> points;
    for (int32_t index = 0; index < teeth; ++index) {
        points.push_back({index * tooth_px, (index % 2) * tooth_px});
        points.push_back({index * tooth_px + 1, (index % 2) * tooth_px});
    }
    return points;
}

[[nodiscard]] std::vector<PointPx> Random_walk_stroke() {
    constexpr int32_t point_count = 800;
    constexpr uint32_t multiplier = 1664525u;
    constexpr uint32_t increment = 1013904223u;
    constexpr uint32_t step_range = 13;
    constexpr int32_t step_offset = 6;
    constexpr uint32_t shift = 16;
    uint32_t state = 12345u;
    PointPx point = {};
    std::vector<PointPx> points;
    for (int32_t index = 0; index < point_count; ++index) {
        state = state * multiplier + increment;
        point.x += static_cast<int32_t>((state >> shift) % step_range) - step_offset;
        state = state * multiplier + increment;
        point.y += static_cast<int32_t>((state >> shift) % step_range) - step_offset;
        points.push_back(point);
    }
    return points;
}

[[nodiscard]] std::vector<PointPx> Duplicate_runs_stroke() {
    // Stationary runs, a return to an earlier pixel, and a straight run.
    return {{0, 0},   {0, 0},   {0, 0},  {5, 0},   {5, 0},   {10, 3},  {0, 0},
            {0, 0},   {-4, 6},  {-8, 6}, {-8, 6},  {-12, 6}, {-16, 6}, {-20, 6},
            {-20, 6}, {-20, 7}, {0, 0},  {30, 30}, {30, 30}, {31, 30}};
}

[[nodiscard]] std::vector<PointPx> Concatenated(FreehandIncrementalSmoother const &s) {
    std::vector<PointPx> all(s.Final_points().begin(), s.Final_points().end());
    all.insert(all.end(), s.Provisional_points().begin(), s.Provisional_points().end());
    return all;
}

void Expect_incremental_matches_batch(std::vector<PointPx> const &points,
                                      FreehandSmoothingMode mode, int32_t width_px,
                                      size_t batch_size) {
    FreehandIncrementalSmoother smoother;
    smoother.Reset(mode, width_px);
    std::vector<PointPx> previous_final;
    std::span<const PointPx> const all(points);
    for (size_t begin = 0; begin < points.size(); begin += batch_size) {
        size_t const count = std::min(batch_size, points.size() - begin);
        smoother.Append(all.subspan(begin, count));
        size_t const prefix = begin + count;
        ASSERT_EQ(smoother.Raw_point_count(), prefix);
        ASSERT_EQ(Concatenated(smoother),
                  Smooth_freehand_points(all.first(prefix), mode, width_px))
            << "prefix " << prefix << " width " << width_px << " batch " << batch_size;
        std::span<const PointPx> const final_points = smoother.Final_points();
        ASSERT_GE(final_points.size(), previous_final.size());
        ASSERT_TRUE(std::equal(previous_final.begin(), previous_final.end(),
                               final_points.begin()))
            << "final points changed at prefix " << prefix;
        previous_final.assign(final_points.begin(), final_points.end());
    }
}

} // namespace

TEST(freehand_smoothing, Incremental_MatchesBatchAtEveryPrefix) {
    constexpr std::array<int32_t, 3> widths = {{1, 11, 40}};
    constexpr std::array<size_t, 4> batch_sizes = {{1, 3, 16, 64}};
    std::vector<std::vector<PointPx>> const strokes = {
        {{0, 0}},
        {{0, 0}, {0, 0}},
        {{0, 0}, {4, 4}, {4, 4}},
        Zigzag_stroke(),
        Spiral_stroke(),
        Random_walk_stroke(),
        Duplicate_runs_stroke(),
    };
    for (FreehandSmoothingMode const mode :
         {FreehandSmoothingMode::Smooth, FreehandSmoothingMode::Off}) {
        for (std::vector<PointPx> const &stroke : strokes) {
            for (int32_t const width : widths) {
                for (size_t const batch : batch_sizes) {
                    Expect_incremental_matches_batch(stroke, mode, width, batch);
                }
            }
        }
    }
}

TEST(freehand_smoothing, Incremental_ResetStartsANewStroke) {
    std::vector<PointPx> const first = Spiral_stroke();
    std::vector<PointPx> const second = Zigzag_stroke();
    FreehandIncrementalSmoother smoother;
    smoother.Reset(FreehandSmoothingMode::Smooth, 11);
    smoother.Append(first);
    smoother.Reset(FreehandSmoothingMode::Smooth, 4);
    smoother.Append(second);
    EXPECT_EQ(smoother.Stroke_width_px(), 4);
    EXPECT_EQ(Concatenated(smoother),
              Smooth_freehand_points(second, FreehandSmoothingMode::Smooth, 4));
}
