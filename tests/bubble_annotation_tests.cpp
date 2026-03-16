#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/annotation_types.h"
#include "greenflame_core/bubble_annotation_types.h"
#include "greenflame_core/color_wheel.h"

using namespace greenflame::core;

namespace {

Annotation Make_bubble(uint64_t id, PointPx center, int32_t diameter_px) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = BubbleAnnotation{
        .center = center,
        .diameter_px = diameter_px,
        .color = Make_colorref(255, 0, 0),
        .counter_value = 1,
    };
    return annotation;
}

} // namespace

// ---------------------------------------------------------------------------
// Bubble_text_color (WCAG luminance)
// ---------------------------------------------------------------------------

TEST(bubble_annotation, TextColor_WhiteBackgroundYieldsBlackText) {
    // Pure white: luminance = 1.0 > 0.179 → black
    EXPECT_EQ(Bubble_text_color(Make_colorref(255, 255, 255)), Make_colorref(0, 0, 0));
}

TEST(bubble_annotation, TextColor_BlackBackgroundYieldsWhiteText) {
    // Pure black: luminance = 0 ≤ 0.179 → white
    EXPECT_EQ(Bubble_text_color(Make_colorref(0, 0, 0)), Make_colorref(255, 255, 255));
}

TEST(bubble_annotation, TextColor_PureRedYieldsBlackText) {
    // Pure red: lum ≈ 0.2126 > 0.179 → black
    EXPECT_EQ(Bubble_text_color(Make_colorref(255, 0, 0)), Make_colorref(0, 0, 0));
}

TEST(bubble_annotation, TextColor_PureBlueYieldsWhiteText) {
    // Pure blue: lum ≈ 0.0722 ≤ 0.179 → white
    EXPECT_EQ(Bubble_text_color(Make_colorref(0, 0, 255)),
              Make_colorref(255, 255, 255));
}

TEST(bubble_annotation, TextColor_MediumGrayYieldsBlackText) {
    // #808080: lum ≈ 0.216 > 0.179 → black
    EXPECT_EQ(Bubble_text_color(Make_colorref(0x80, 0x80, 0x80)),
              Make_colorref(0, 0, 0));
}

TEST(bubble_annotation, TextColor_YellowYieldsBlackText) {
    // #FFFF00: lum ≈ 0.928 > 0.179 → black
    EXPECT_EQ(Bubble_text_color(Make_colorref(0xFF, 0xFF, 0x00)),
              Make_colorref(0, 0, 0));
}

TEST(bubble_annotation, TextColor_MediumGreenYieldsWhiteText) {
    // #008000: lum ≈ 0.154 ≤ 0.179 → white
    EXPECT_EQ(Bubble_text_color(Make_colorref(0x00, 0x80, 0x00)),
              Make_colorref(255, 255, 255));
}

// ---------------------------------------------------------------------------
// Annotation_hits_point (circle distance check)
// ---------------------------------------------------------------------------

TEST(bubble_annotation, HitsPoint_CenterIsHit) {
    Annotation const bubble = Make_bubble(1, {50, 50}, 20);
    EXPECT_TRUE(Annotation_hits_point(bubble, {50, 50}));
}

TEST(bubble_annotation, HitsPoint_PointJustInsideRadiusIsHit) {
    // diameter=20 → r=10; point (40, 50): dx=40.5-50=-9.5, dy=0.5, dist²=90.5 ≤ 100
    Annotation const bubble = Make_bubble(2, {50, 50}, 20);
    EXPECT_TRUE(Annotation_hits_point(bubble, {40, 50}));
}

TEST(bubble_annotation, HitsPoint_PointJustOutsideRadiusIsMiss) {
    // point (39, 50): dx=39.5-50=-10.5, dy=0.5, dist²=110.5 > 100
    Annotation const bubble = Make_bubble(3, {50, 50}, 20);
    EXPECT_FALSE(Annotation_hits_point(bubble, {39, 50}));
}

TEST(bubble_annotation, HitsPoint_ZeroDiameterIsMiss) {
    Annotation const bubble = Make_bubble(4, {50, 50}, 0);
    EXPECT_FALSE(Annotation_hits_point(bubble, {50, 50}));
}

// ---------------------------------------------------------------------------
// Annotation_visual_bounds / Annotation_bounds
// ---------------------------------------------------------------------------

TEST(bubble_annotation, VisualBounds_MatchesCenterAndDiameter) {
    Annotation const bubble = Make_bubble(5, {100, 80}, 20);
    RectPx const bounds = Annotation_visual_bounds(bubble);
    EXPECT_EQ(bounds.left, 90);
    EXPECT_EQ(bounds.top, 70);
    EXPECT_EQ(bounds.right, 110);
    EXPECT_EQ(bounds.bottom, 90);
}

// ---------------------------------------------------------------------------
// Translate_annotation
// ---------------------------------------------------------------------------

TEST(bubble_annotation, Translate_ShiftsCenter) {
    Annotation const bubble = Make_bubble(6, {100, 100}, 20);
    Annotation const moved = Translate_annotation(bubble, {10, -5});
    auto const &moved_bubble = std::get<BubbleAnnotation>(moved.data);
    EXPECT_EQ(moved_bubble.center, (PointPx{110, 95}));
}

TEST(bubble_annotation, Translate_PreservesOtherFields) {
    Annotation const bubble = Make_bubble(7, {50, 50}, 30);
    Annotation const moved = Translate_annotation(bubble, {1, 1});
    auto const &moved_bubble = std::get<BubbleAnnotation>(moved.data);
    EXPECT_EQ(moved_bubble.diameter_px, 30);
    EXPECT_EQ(moved_bubble.counter_value, 1);
}

// ---------------------------------------------------------------------------
// Annotation_shows_corner_brackets
// ---------------------------------------------------------------------------

TEST(bubble_annotation, ShowsCornerBrackets_ReturnsTrue) {
    EXPECT_TRUE(Annotation_shows_corner_brackets(AnnotationKind::Bubble));
}

// ---------------------------------------------------------------------------
// Annotation::Kind
// ---------------------------------------------------------------------------

TEST(bubble_annotation, Kind_ReturnsBubble) {
    Annotation const bubble = Make_bubble(9, {50, 50}, 20);
    EXPECT_EQ(bubble.Kind(), AnnotationKind::Bubble);
}
