#include "greenflame_core/annotation_hit_test.h"

using namespace greenflame::core;

namespace {

Annotation Make_line(uint64_t id, PointPx start, PointPx end,
                     int32_t width_px = StrokeStyle::kDefaultWidthPx,
                     bool arrow_head = false) {
    Annotation annotation{};
    annotation.id = id;
    annotation.kind = AnnotationKind::Line;
    annotation.line.start = start;
    annotation.line.end = end;
    annotation.line.style.width_px = width_px;
    annotation.line.arrow_head = arrow_head;
    return annotation;
}

Annotation Make_rectangle(uint64_t id, RectPx outer_bounds, int32_t width_px,
                          bool filled = false) {
    Annotation annotation{};
    annotation.id = id;
    annotation.kind = AnnotationKind::Rectangle;
    annotation.rectangle.outer_bounds = outer_bounds;
    annotation.rectangle.style.width_px = width_px;
    annotation.rectangle.filled = filled;
    return annotation;
}

} // namespace

TEST(annotation_hit_test, AnnotationHitsPoint_LineUsesSquareCaps) {
    Annotation const line = Make_line(7, {10, 10}, {20, 10}, 4);

    EXPECT_TRUE(Annotation_hits_point(line, {8, 10}));
    EXPECT_TRUE(Annotation_hits_point(line, {21, 10}));
    EXPECT_FALSE(Annotation_hits_point(line, {7, 10}));
    EXPECT_FALSE(Annotation_hits_point(line, {22, 10}));
}

TEST(annotation_hit_test, AnnotationHitsPoint_ArrowCoversTipAndHeadBase) {
    Annotation const arrow = Make_line(8, {10, 10}, {50, 10}, 4, true);

    EXPECT_TRUE(Annotation_hits_point(arrow, {50, 10}));
    EXPECT_TRUE(Annotation_hits_point(arrow, {40, 7}));
    EXPECT_FALSE(Annotation_hits_point(arrow, {50, 2}));
}

TEST(annotation_hit_test, TranslateAnnotation_LineMovesEndpoints) {
    Annotation const line = Make_line(9, {20, 30}, {40, 30}, 6);

    Annotation const moved = Translate_annotation(line, {5, -3});

    EXPECT_EQ(moved.line.start, (PointPx{25, 27}));
    EXPECT_EQ(moved.line.end, (PointPx{45, 27}));
}

TEST(annotation_hit_test, TranslateAnnotation_ArrowMovesEndpoints) {
    Annotation const arrow = Make_line(10, {20, 30}, {60, 45}, 6, true);

    Annotation const moved = Translate_annotation(arrow, {5, -3});

    EXPECT_TRUE(moved.line.arrow_head);
    EXPECT_EQ(moved.line.start, (PointPx{25, 27}));
    EXPECT_EQ(moved.line.end, (PointPx{65, 42}));
}

TEST(annotation_hit_test, HitTestLineEndpointHandles_FavorsNearestVisibleHandle) {
    EXPECT_EQ(Hit_test_line_endpoint_handles({30, 40}, {90, 40}, {30, 40}),
              std::optional<AnnotationLineEndpoint>{AnnotationLineEndpoint::Start});
    EXPECT_EQ(Hit_test_line_endpoint_handles({30, 40}, {90, 40}, {90, 40}),
              std::optional<AnnotationLineEndpoint>{AnnotationLineEndpoint::End});
    EXPECT_EQ(Hit_test_line_endpoint_handles({50, 50}, {53, 50}, {51, 50}),
              std::optional<AnnotationLineEndpoint>{AnnotationLineEndpoint::Start});
    EXPECT_EQ(Hit_test_line_endpoint_handles({30, 40}, {90, 40}, {60, 40}),
              std::nullopt);
}

TEST(annotation_hit_test, HitTestLineEndpointHandles_UsesElevenPixelPickupArea) {
    EXPECT_EQ(Hit_test_line_endpoint_handles({30, 40}, {90, 40}, {35, 45}),
              std::optional<AnnotationLineEndpoint>{AnnotationLineEndpoint::Start});
    EXPECT_EQ(Hit_test_line_endpoint_handles({30, 40}, {90, 40}, {36, 45}),
              std::nullopt);
}

TEST(annotation_hit_test, AnnotationHitsPoint_RectangleOuterBoundaryHit) {
    Annotation const rectangle =
        Make_rectangle(1, RectPx::From_ltrb(10, 20, 31, 41), 4, false);

    EXPECT_TRUE(Annotation_hits_point(rectangle, {10, 20}));
    EXPECT_TRUE(Annotation_hits_point(rectangle, {30, 40}));
    EXPECT_FALSE(Annotation_hits_point(rectangle, {31, 40}));
}

TEST(annotation_hit_test, RasterizeRectangle_OutlineStaysInsetFromOuterEdge) {
    Annotation const rectangle =
        Make_rectangle(1, RectPx::From_ltrb(10, 10, 21, 21), 3, false);

    EXPECT_TRUE(Annotation_hits_point(rectangle, {10, 10}));
    EXPECT_TRUE(Annotation_hits_point(rectangle, {20, 20}));
    EXPECT_FALSE(Annotation_hits_point(rectangle, {13, 13}));
}

TEST(annotation_hit_test, RasterizeRectangle_FilledIgnoresInteriorHole) {
    Annotation const rectangle =
        Make_rectangle(1, RectPx::From_ltrb(10, 10, 21, 21), 8, true);

    EXPECT_TRUE(Annotation_hits_point(rectangle, {10, 10}));
    EXPECT_TRUE(Annotation_hits_point(rectangle, {15, 15}));
    EXPECT_TRUE(Annotation_hits_point(rectangle, {20, 20}));
}

TEST(annotation_hit_test,
     RectangleResizeHandles_HideSideHandlesWhenTheyOverlapCorners) {
    std::array<bool, 8> const visible =
        Visible_rectangle_resize_handles(RectPx::From_ltrb(10, 10, 18, 18));

    EXPECT_TRUE(visible[static_cast<size_t>(SelectionHandle::TopLeft)]);
    EXPECT_TRUE(visible[static_cast<size_t>(SelectionHandle::TopRight)]);
    EXPECT_TRUE(visible[static_cast<size_t>(SelectionHandle::BottomRight)]);
    EXPECT_TRUE(visible[static_cast<size_t>(SelectionHandle::BottomLeft)]);
    EXPECT_FALSE(visible[static_cast<size_t>(SelectionHandle::Top)]);
    EXPECT_FALSE(visible[static_cast<size_t>(SelectionHandle::Right)]);
    EXPECT_FALSE(visible[static_cast<size_t>(SelectionHandle::Bottom)]);
    EXPECT_FALSE(visible[static_cast<size_t>(SelectionHandle::Left)]);
}

TEST(annotation_hit_test, HitTestRectangleResizeHandles_PrefersVisibleCorners) {
    EXPECT_EQ(
        Hit_test_rectangle_resize_handles(RectPx::From_ltrb(10, 10, 18, 18), {10, 10}),
        std::optional<SelectionHandle>{SelectionHandle::TopLeft});
    EXPECT_EQ(
        Hit_test_rectangle_resize_handles(RectPx::From_ltrb(10, 10, 40, 40), {25, 10}),
        std::optional<SelectionHandle>{SelectionHandle::Top});
}

TEST(annotation_hit_test, ResizeRectangleFromHandle_UsesInclusiveCursorForRightEdge) {
    RectPx const resized = Resize_rectangle_from_handle(
        RectPx::From_ltrb(10, 10, 21, 21), SelectionHandle::Right, {30, 15});

    EXPECT_EQ(resized, (RectPx::From_ltrb(10, 10, 31, 21)));
}

TEST(annotation_hit_test, TranslateAnnotation_RectangleMovesBounds) {
    Annotation const rectangle =
        Make_rectangle(2, RectPx::From_ltrb(10, 10, 21, 21), 2, false);

    Annotation const moved = Translate_annotation(rectangle, {5, -3});

    EXPECT_EQ(moved.rectangle.outer_bounds, (RectPx::From_ltrb(15, 7, 26, 18)));
}
