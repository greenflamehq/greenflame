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
