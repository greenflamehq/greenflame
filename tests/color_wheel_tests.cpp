#include "greenflame_core/color_wheel.h"

using namespace greenflame::core;

namespace {

constexpr size_t kTextStyleWheelSegmentCount = 12;

PointPx Point_on_wheel(PointPx center, float radius, float angle_degrees) {
    float const radians = angle_degrees * 3.14159265358979323846f / 180.0f;
    int32_t const x =
        center.x + static_cast<int32_t>(std::lround(std::cos(radians) * radius));
    int32_t const y =
        center.y + static_cast<int32_t>(std::lround(std::sin(radians) * radius));
    return {x, y};
}

float Mid_radius_px() {
    float const outer_radius = static_cast<float>(kColorWheelOuterDiameterPx) / 2.0f;
    float const inner_radius = outer_radius - kColorWheelWidthPx;
    return (outer_radius + inner_radius) / 2.0f;
}

float Hub_r_px() {
    float const outer = static_cast<float>(kColorWheelOuterDiameterPx) / 2.0f;
    return outer - kColorWheelWidthPx - kTextWheelHubRingGapPx;
}

float Degrees_per_segment(size_t segment_count) {
    return 360.0f / static_cast<float>(segment_count);
}

} // namespace

TEST(color_wheel, DefaultPaletteMatchesExpectedOrder) {
    EXPECT_EQ(kDefaultAnnotationColorPalette[0], Make_colorref(0x00, 0x00, 0x00));
    EXPECT_EQ(kDefaultAnnotationColorPalette[1], Make_colorref(0xFF, 0x00, 0x00));
    EXPECT_EQ(kDefaultAnnotationColorPalette[2], Make_colorref(0x00, 0xFF, 0x00));
    EXPECT_EQ(kDefaultAnnotationColorPalette[3], Make_colorref(0x00, 0x00, 0xFF));
    EXPECT_EQ(kDefaultAnnotationColorPalette[4], Make_colorref(0xFF, 0xFF, 0x00));
    EXPECT_EQ(kDefaultAnnotationColorPalette[5], Make_colorref(0xFF, 0x00, 0xFF));
    EXPECT_EQ(kDefaultAnnotationColorPalette[6], Make_colorref(0x00, 0xFF, 0xFF));
    EXPECT_EQ(kDefaultAnnotationColorPalette[7], Make_colorref(0xFF, 0xFF, 0xFF));
}

TEST(color_wheel, DefaultHighlighterPaletteMatchesExpectedOrder) {
    EXPECT_EQ(kDefaultHighlighterColorPalette[0], Make_colorref(0xF7, 0xEB, 0x62));
    EXPECT_EQ(kDefaultHighlighterColorPalette[1], Make_colorref(0x7F, 0xE3, 0x6A));
    EXPECT_EQ(kDefaultHighlighterColorPalette[2], Make_colorref(0xFF, 0xB4, 0x4D));
    EXPECT_EQ(kDefaultHighlighterColorPalette[3], Make_colorref(0xFF, 0x79, 0xB9));
    EXPECT_EQ(kDefaultHighlighterColorPalette[4], Make_colorref(0x64, 0xC7, 0xFF));
    EXPECT_EQ(kDefaultHighlighterColorPalette[5], Make_colorref(0xC3, 0x8C, 0xFF));
}

TEST(color_wheel, ClampAnnotationColorIndex_ClampsToSupportedRange) {
    EXPECT_EQ(Clamp_annotation_color_index(-10), 0);
    EXPECT_EQ(Clamp_annotation_color_index(0), 0);
    EXPECT_EQ(Clamp_annotation_color_index(7), 7);
    EXPECT_EQ(Clamp_annotation_color_index(99), 7);
}

TEST(color_wheel, ClampHighlighterColorIndex_ClampsToSupportedRange) {
    EXPECT_EQ(Clamp_highlighter_color_index(-10), 0);
    EXPECT_EQ(Clamp_highlighter_color_index(0), 0);
    EXPECT_EQ(Clamp_highlighter_color_index(5), 5);
    EXPECT_EQ(Clamp_highlighter_color_index(99), 5);
}

TEST(color_wheel, SegmentGeometry_StartsAtTopRightAndSweepsClockwiseForEightSegments) {
    ColorWheelSegmentGeometry const first =
        Get_color_wheel_segment_geometry(0, kAnnotationColorSlotCount);
    ColorWheelSegmentGeometry const second =
        Get_color_wheel_segment_geometry(1, kAnnotationColorSlotCount);
    ColorWheelSegmentGeometry const last = Get_color_wheel_segment_geometry(
        kAnnotationColorSlotCount - 1, kAnnotationColorSlotCount);
    float const degrees_per_segment = Degrees_per_segment(kAnnotationColorSlotCount);
    float const top_gap_center_degrees = 270.0f;

    EXPECT_NEAR(first.center_angle_degrees,
                top_gap_center_degrees + degrees_per_segment / 2.0f, 0.01f);
    EXPECT_GT(first.sweep_angle_degrees, 0.0f);
    EXPECT_LT(first.sweep_angle_degrees, degrees_per_segment);
    EXPECT_NEAR(second.center_angle_degrees,
                top_gap_center_degrees + 3.0f * degrees_per_segment / 2.0f, 0.01f);
    EXPECT_NEAR(last.center_angle_degrees,
                top_gap_center_degrees - degrees_per_segment / 2.0f, 0.01f);
}

TEST(color_wheel, SegmentGeometry_SupportsConfigurableSixSegments) {
    ColorWheelSegmentGeometry const first =
        Get_color_wheel_segment_geometry(0, kHighlighterColorSlotCount);
    ColorWheelSegmentGeometry const last = Get_color_wheel_segment_geometry(
        kHighlighterColorSlotCount - 1, kHighlighterColorSlotCount);
    float const degrees_per_segment = Degrees_per_segment(kHighlighterColorSlotCount);
    float const top_gap_center_degrees = 270.0f;

    EXPECT_NEAR(first.center_angle_degrees,
                top_gap_center_degrees + degrees_per_segment / 2.0f, 0.01f);
    EXPECT_NEAR(last.center_angle_degrees,
                top_gap_center_degrees - degrees_per_segment / 2.0f, 0.01f);
}

TEST(color_wheel, SegmentGeometry_SupportsConfigurableTwelveSegments) {
    ColorWheelSegmentGeometry const first =
        Get_color_wheel_segment_geometry(0, kTextStyleWheelSegmentCount);
    ColorWheelSegmentGeometry const last = Get_color_wheel_segment_geometry(
        kTextStyleWheelSegmentCount - 1, kTextStyleWheelSegmentCount);
    ColorWheelSegmentGeometry const first_font_slot =
        Get_color_wheel_segment_geometry(8, kTextStyleWheelSegmentCount);
    float const degrees_per_segment = Degrees_per_segment(kTextStyleWheelSegmentCount);
    float const top_gap_center_degrees = 270.0f;

    EXPECT_NEAR(first.center_angle_degrees,
                top_gap_center_degrees + degrees_per_segment / 2.0f, 0.01f);
    EXPECT_NEAR(last.center_angle_degrees,
                top_gap_center_degrees - degrees_per_segment / 2.0f, 0.01f);
    EXPECT_NEAR(first_font_slot.center_angle_degrees,
                top_gap_center_degrees - 7.0f * degrees_per_segment / 2.0f, 0.01f);

    for (size_t index = 0; index < kTextStyleWheelSegmentCount; ++index) {
        ColorWheelSegmentGeometry const geometry =
            Get_color_wheel_segment_geometry(index, kTextStyleWheelSegmentCount);
        EXPECT_GT(geometry.sweep_angle_degrees, 0.0f);
        EXPECT_LT(geometry.sweep_angle_degrees, degrees_per_segment);
    }
}

TEST(color_wheel, HitTest_ReturnsSegmentForEachEightSegmentCenter) {
    PointPx const center{500, 500};
    float const radius = Mid_radius_px();

    for (size_t index = 0; index < kAnnotationColorSlotCount; ++index) {
        ColorWheelSegmentGeometry const geometry =
            Get_color_wheel_segment_geometry(index, kAnnotationColorSlotCount);
        EXPECT_EQ(Hit_test_color_wheel_segment(
                      center,
                      Point_on_wheel(center, radius, geometry.center_angle_degrees),
                      kAnnotationColorSlotCount),
                  std::optional<size_t>{index});
    }
}

TEST(color_wheel, HitTest_ReturnsSegmentForEachSixSegmentCenter) {
    PointPx const center{500, 500};
    float const radius = Mid_radius_px();

    for (size_t index = 0; index < kHighlighterColorSlotCount; ++index) {
        ColorWheelSegmentGeometry const geometry =
            Get_color_wheel_segment_geometry(index, kHighlighterColorSlotCount);
        EXPECT_EQ(Hit_test_color_wheel_segment(
                      center,
                      Point_on_wheel(center, radius, geometry.center_angle_degrees),
                      kHighlighterColorSlotCount),
                  std::optional<size_t>{index});
    }
}

TEST(color_wheel, HitTest_ReturnsSegmentForEachTwelveSegmentCenter) {
    PointPx const center{500, 500};
    float const radius = Mid_radius_px();

    for (size_t index = 0; index < kTextStyleWheelSegmentCount; ++index) {
        ColorWheelSegmentGeometry const geometry =
            Get_color_wheel_segment_geometry(index, kTextStyleWheelSegmentCount);
        EXPECT_EQ(Hit_test_color_wheel_segment(
                      center,
                      Point_on_wheel(center, radius, geometry.center_angle_degrees),
                      kTextStyleWheelSegmentCount),
                  std::optional<size_t>{index});
    }
}

TEST(color_wheel, HitTest_ReturnsNulloptForInnerHoleOuterBoundsAndGap) {
    PointPx const center{300, 300};
    float const outer_radius = static_cast<float>(kColorWheelOuterDiameterPx) / 2.0f;

    EXPECT_EQ(Hit_test_color_wheel_segment(center, center, kAnnotationColorSlotCount),
              std::nullopt);
    EXPECT_EQ(Hit_test_color_wheel_segment(
                  center, Point_on_wheel(center, outer_radius + 10.0f, 0.0f),
                  kAnnotationColorSlotCount),
              std::nullopt);
    EXPECT_EQ(Hit_test_color_wheel_segment(
                  center, Point_on_wheel(center, Mid_radius_px(), 270.0f),
                  kAnnotationColorSlotCount),
              std::nullopt);
}

TEST(color_wheel, HitTest_TextStyleWheelReturnsNulloptForTwelveSegmentGap) {
    PointPx const center{300, 300};

    EXPECT_EQ(Hit_test_color_wheel_segment(
                  center, Point_on_wheel(center, Mid_radius_px(), 270.0f),
                  kTextStyleWheelSegmentCount),
              std::nullopt);
}

TEST(color_wheel, HubHitTest_ReturnsNulloptOutsideHubRadius) {
    PointPx const center{500, 500};
    float const hub_r = Hub_r_px();
    PointPx const outside{center.x + static_cast<int32_t>(hub_r) + 1, center.y};
    EXPECT_EQ(Hit_test_text_wheel_hub(center, outside), std::nullopt);
}

TEST(color_wheel, HubHitTest_ReturnsNulloptInVerticalGap) {
    PointPx const center{500, 500};
    // Exact center falls in the gap.
    EXPECT_EQ(Hit_test_text_wheel_hub(center, center), std::nullopt);
    // Point at left edge of gap.
    PointPx const left_gap{center.x - static_cast<int32_t>(kTextWheelHubHalfGapPx),
                           center.y};
    EXPECT_EQ(Hit_test_text_wheel_hub(center, left_gap), std::nullopt);
    // Point at right edge of gap.
    PointPx const right_gap{center.x + static_cast<int32_t>(kTextWheelHubHalfGapPx),
                            center.y};
    EXPECT_EQ(Hit_test_text_wheel_hub(center, right_gap), std::nullopt);
}

TEST(color_wheel, HubHitTest_ReturnsColorForLeftHalf) {
    PointPx const center{500, 500};
    float const hub_r = Hub_r_px();
    PointPx const left{center.x - static_cast<int32_t>(hub_r / 2.f), center.y};
    EXPECT_EQ(Hit_test_text_wheel_hub(center, left),
              std::optional<TextWheelHubSide>{TextWheelHubSide::Color});
}

TEST(color_wheel, HubHitTest_ReturnsFontForRightHalf) {
    PointPx const center{500, 500};
    float const hub_r = Hub_r_px();
    PointPx const right{center.x + static_cast<int32_t>(hub_r / 2.f), center.y};
    EXPECT_EQ(Hit_test_text_wheel_hub(center, right),
              std::optional<TextWheelHubSide>{TextWheelHubSide::Font});
}
