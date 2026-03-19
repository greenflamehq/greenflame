#include "greenflame_core/annotation_hit_test.h"

using namespace greenflame::core;

namespace {

Annotation Make_freehand(uint64_t id, std::initializer_list<PointPx> points,
                         StrokeStyle style = {},
                         FreehandTipShape tip_shape = FreehandTipShape::Round) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = FreehandStrokeAnnotation{
        .points = std::vector<PointPx>(points),
        .style = style,
        .freehand_tip_shape = tip_shape,
    };
    return annotation;
}

Annotation Make_line(uint64_t id, PointPx start, PointPx end,
                     int32_t width_px = StrokeStyle::kDefaultWidthPx,
                     bool arrow_head = false) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = LineAnnotation{
        .start = start,
        .end = end,
        .style = {.width_px = width_px},
        .arrow_head = arrow_head,
    };
    return annotation;
}

Annotation Make_rectangle(uint64_t id, RectPx outer_bounds, int32_t width_px,
                          bool filled = false) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = RectangleAnnotation{
        .outer_bounds = outer_bounds,
        .style = {.width_px = width_px},
        .filled = filled,
    };
    return annotation;
}

Annotation Make_ellipse(uint64_t id, RectPx outer_bounds, int32_t width_px,
                        bool filled = false) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = EllipseAnnotation{
        .outer_bounds = outer_bounds,
        .style = {.width_px = width_px},
        .filled = filled,
    };
    return annotation;
}

Annotation Make_text(uint64_t id, PointPx origin, RectPx visual_bounds,
                     std::vector<uint8_t> premultiplied_bgra) {
    Annotation annotation{};
    annotation.id = id;
    TextAnnotation text{};
    text.origin = origin;
    text.visual_bounds = visual_bounds;
    text.bitmap_width_px = visual_bounds.Width();
    text.bitmap_height_px = visual_bounds.Height();
    text.bitmap_row_bytes = text.bitmap_width_px * 4;
    text.premultiplied_bgra = std::move(premultiplied_bgra);
    annotation.data = std::move(text);
    return annotation;
}

} // namespace

TEST(annotation_hit_test, AnnotationHitsPoint_SquareFreehandUsesSquareTipCoverage) {
    Annotation const square_stroke = Make_freehand(
        6, {{10, 10}}, StrokeStyle{.width_px = 4}, FreehandTipShape::Square);

    EXPECT_TRUE(Annotation_hits_point(square_stroke, {11, 11}));
    EXPECT_FALSE(Annotation_hits_point(square_stroke, {12, 12}));
}

TEST(annotation_hit_test,
     AnnotationHitsPoint_SquareFreehandDiagonalUsesAxisAlignedSquareSweep) {
    Annotation const square_stroke = Make_freehand(
        7, {{10, 10}, {14, 14}}, StrokeStyle{.width_px = 2}, FreehandTipShape::Square);

    EXPECT_TRUE(Annotation_hits_point(square_stroke, {13, 11}));
    EXPECT_FALSE(Annotation_hits_point(square_stroke, {14, 10}));
}

TEST(annotation_hit_test, AnnotationHitsPoint_LineUsesSquareCaps) {
    Annotation const line = Make_line(8, {10, 10}, {20, 10}, 4);

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

    auto const &moved_line = std::get<LineAnnotation>(moved.data);
    EXPECT_EQ(moved_line.start, (PointPx{25, 27}));
    EXPECT_EQ(moved_line.end, (PointPx{45, 27}));
}

TEST(annotation_hit_test, TranslateAnnotation_ArrowMovesEndpoints) {
    Annotation const arrow = Make_line(10, {20, 30}, {60, 45}, 6, true);

    Annotation const moved = Translate_annotation(arrow, {5, -3});

    auto const &moved_arrow = std::get<LineAnnotation>(moved.data);
    EXPECT_TRUE(moved_arrow.arrow_head);
    EXPECT_EQ(moved_arrow.start, (PointPx{25, 27}));
    EXPECT_EQ(moved_arrow.end, (PointPx{65, 42}));
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

TEST(annotation_hit_test, AnnotationHitsPoint_EllipseOutlineUsesInteriorHole) {
    Annotation const ellipse =
        Make_ellipse(11, RectPx::From_ltrb(10, 10, 31, 31), 4, false);

    EXPECT_TRUE(Annotation_hits_point(ellipse, {20, 10}));
    EXPECT_TRUE(Annotation_hits_point(ellipse, {10, 20}));
    EXPECT_FALSE(Annotation_hits_point(ellipse, {20, 20}));
}

TEST(annotation_hit_test, AnnotationHitsPoint_FilledEllipseCoversInterior) {
    Annotation const ellipse =
        Make_ellipse(12, RectPx::From_ltrb(10, 10, 31, 31), 4, true);

    EXPECT_TRUE(Annotation_hits_point(ellipse, {20, 20}));
    EXPECT_TRUE(Annotation_hits_point(ellipse, {20, 10}));
    EXPECT_FALSE(Annotation_hits_point(ellipse, {10, 10}));
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

    EXPECT_EQ(std::get<RectangleAnnotation>(moved.data).outer_bounds,
              (RectPx::From_ltrb(15, 7, 26, 18)));
}

TEST(annotation_hit_test, TranslateAnnotation_EllipseMovesBounds) {
    Annotation const ellipse = Make_ellipse(13, RectPx::From_ltrb(10, 10, 21, 21), 2);

    Annotation const moved = Translate_annotation(ellipse, {5, -3});

    EXPECT_EQ(std::get<EllipseAnnotation>(moved.data).outer_bounds,
              (RectPx::From_ltrb(15, 7, 26, 18)));
}

TEST(annotation_hit_test, AnnotationHitsPoint_TextUsesBitmapAlphaCoverage) {
    std::vector<uint8_t> bitmap(16u, 0);
    bitmap[7] = 255;  // pixel (1, 0) alpha
    bitmap[11] = 255; // pixel (0, 1) alpha
    Annotation const text =
        Make_text(3, {40, 50}, RectPx::From_ltrb(40, 50, 42, 52), std::move(bitmap));

    EXPECT_FALSE(Annotation_hits_point(text, {40, 50}));
    EXPECT_TRUE(Annotation_hits_point(text, {41, 50}));
    EXPECT_TRUE(Annotation_hits_point(text, {40, 51}));
    EXPECT_FALSE(Annotation_hits_point(text, {41, 51}));
}

TEST(annotation_hit_test, TranslateAnnotation_TextMovesOriginAndBounds) {
    std::vector<uint8_t> bitmap(16u, 0);
    Annotation const text =
        Make_text(4, {40, 50}, RectPx::From_ltrb(40, 50, 42, 52), std::move(bitmap));

    Annotation const moved = Translate_annotation(text, {5, -3});

    auto const &moved_text = std::get<TextAnnotation>(moved.data);
    EXPECT_EQ(moved_text.origin, (PointPx{45, 47}));
    EXPECT_EQ(moved_text.visual_bounds, (RectPx::From_ltrb(45, 47, 47, 49)));
}

TEST(annotation_hit_test, AnnotationShowsCornerBrackets_FreehandReturnsTrue) {
    EXPECT_TRUE(Annotation_shows_corner_brackets(AnnotationKind::Freehand));
}

TEST(annotation_hit_test, AnnotationShowsCornerBrackets_LineReturnsTrue) {
    EXPECT_TRUE(Annotation_shows_corner_brackets(AnnotationKind::Line));
}

TEST(annotation_hit_test, AnnotationShowsCornerBrackets_RectangleReturnsFalse) {
    EXPECT_FALSE(Annotation_shows_corner_brackets(AnnotationKind::Rectangle));
}

TEST(annotation_hit_test, AnnotationShowsCornerBrackets_EllipseReturnsFalse) {
    EXPECT_FALSE(Annotation_shows_corner_brackets(AnnotationKind::Ellipse));
}

TEST(annotation_hit_test, AnnotationVisualBounds_FreehandMatchesHitTestBounds) {
    Annotation ann{};
    ann.id = 1;
    ann.data = FreehandStrokeAnnotation{
        .points = {PointPx{10, 20}, PointPx{30, 40}, PointPx{50, 20}},
        .style = {.width_px = 4},
    };

    EXPECT_EQ(Annotation_visual_bounds(ann), Annotation_bounds(ann));
}

TEST(annotation_hit_test, AnnotationVisualBounds_RectangleMatchesHitTestBounds) {
    Annotation const rect = Make_rectangle(1, RectPx::From_ltrb(10, 20, 50, 60), 4);

    EXPECT_EQ(Annotation_visual_bounds(rect), Annotation_bounds(rect));
}

TEST(annotation_hit_test, AnnotationVisualBounds_EllipseMatchesHitTestBounds) {
    Annotation const ellipse = Make_ellipse(1, RectPx::From_ltrb(10, 20, 50, 60), 4);

    EXPECT_EQ(Annotation_visual_bounds(ellipse), Annotation_bounds(ellipse));
}

TEST(annotation_hit_test,
     BlendAnnotationOntoPixels_FilledEllipse_PaintsInteriorSkipsCorner) {
    // Ellipse bounds [0,0,10,10]: rx=ry=5, center=(5,5).
    // Pixel (4,4) center (4.5,4.5): dx=dy=-0.1, sum=0.02 — inside → painted.
    // Pixel (0,0) center (0.5,0.5): dx=dy=-0.9, sum=1.62 — outside corner → not
    // painted.
    Annotation ann{};
    ann.id = 1;
    ann.data = EllipseAnnotation{
        .outer_bounds = RectPx::From_ltrb(0, 0, 10, 10),
        .style = {.width_px = 2, .color = RGB(0xFF, 0x00, 0x00)},
        .filled = true,
    };
    std::array<uint8_t, 4> const background = {10, 20, 30, 255};

    {
        auto px = background;
        Blend_annotation_onto_pixels(px, 1, 1, 4, ann, RectPx::From_ltrb(4, 4, 5, 5));
        EXPECT_EQ(px[0], 0x00); // blue of RGB(0xFF,0,0)
        EXPECT_EQ(px[1], 0x00); // green
        EXPECT_EQ(px[2], 0xFF); // red
        EXPECT_EQ(px[3], 255);
    }
    {
        auto px = background;
        Blend_annotation_onto_pixels(px, 1, 1, 4, ann, RectPx::From_ltrb(0, 0, 1, 1));
        EXPECT_EQ(px, background); // outside ellipse — unchanged
    }
}

TEST(annotation_hit_test,
     BlendAnnotationOntoPixels_EllipseOutline_PaintsEdgeSkipsCenter) {
    // Ellipse bounds [0,0,10,10], width=2: inner bounds [2,2,8,8], inner rx=ry=3.
    // Edge pixel (0,4) center (0.5,4.5): outer sum=0.82 ≤1, inner sum=2.28 >1 →
    // painted. Center pixel (4,4) center (4.5,4.5): outer sum=0.02 ≤1, inner sum=0.056
    // ≤1 → not painted.
    Annotation ann{};
    ann.id = 1;
    ann.data = EllipseAnnotation{
        .outer_bounds = RectPx::From_ltrb(0, 0, 10, 10),
        .style = {.width_px = 2, .color = RGB(0xFF, 0x00, 0x00)},
        .filled = false,
    };
    std::array<uint8_t, 4> const background = {10, 20, 30, 255};

    {
        auto px = background;
        Blend_annotation_onto_pixels(px, 1, 1, 4, ann, RectPx::From_ltrb(0, 4, 1, 5));
        EXPECT_EQ(px[0], 0x00);
        EXPECT_EQ(px[1], 0x00);
        EXPECT_EQ(px[2], 0xFF);
        EXPECT_EQ(px[3], 255);
    }
    {
        auto px = background;
        Blend_annotation_onto_pixels(px, 1, 1, 4, ann, RectPx::From_ltrb(4, 4, 5, 5));
        EXPECT_EQ(px, background); // inside inner hole — unchanged
    }
}

TEST(annotation_hit_test, BlendAnnotationOntoPixels_UsesOpacityForFreehandStroke) {
    std::array<uint8_t, 4> pixels = {100, 150, 200, 255};
    Annotation const stroke = Make_freehand(1, {{0, 0}},
                                            StrokeStyle{.width_px = 2,
                                                        .color = RGB(0x14, 0x28, 0x3C),
                                                        .opacity_percent = 50},
                                            FreehandTipShape::Square);

    Blend_annotation_onto_pixels(pixels, 1, 1, 4, stroke,
                                 RectPx::From_ltrb(0, 0, 1, 1));

    auto const blend = [](uint8_t dst, uint8_t src) {
        uint8_t const alpha = 128;
        uint32_t const src_term = static_cast<uint32_t>(src) * alpha;
        uint32_t const dst_term =
            static_cast<uint32_t>(dst) * (static_cast<uint32_t>(255) - alpha);
        return static_cast<uint8_t>((src_term + dst_term + 127) / 255);
    };

    EXPECT_EQ(pixels[0], blend(100, 0x3C));
    EXPECT_EQ(pixels[1], blend(150, 0x28));
    EXPECT_EQ(pixels[2], blend(200, 0x14));
    EXPECT_EQ(pixels[3], 255);
}

TEST(annotation_hit_test, AnnotationVisualBounds_HorizontalLineExcludesCapAndPadding) {
    // 2px wide line from (10,100) to (200,100).
    // Tight rectangle corners: (10,99), (10,101), (200,99), (200,101).
    // -> From_ltrb(10, 99, 201, 102)
    // Hit-test bounds are larger due to cap extension and floor-1/ceil+2 padding.
    Annotation const line = Make_line(1, {10, 100}, {200, 100}, 2);

    RectPx const visual = Annotation_visual_bounds(line);
    RectPx const hit = Annotation_bounds(line);

    EXPECT_EQ(visual, (RectPx::From_ltrb(10, 99, 201, 102)));
    EXPECT_GT(visual.left, hit.left);
    EXPECT_GT(visual.top, hit.top);
    EXPECT_LT(visual.right, hit.right);
    EXPECT_LT(visual.bottom, hit.bottom);
}

TEST(annotation_hit_test, AnnotationVisualBounds_VerticalLineExcludesCapExtension) {
    // 4px wide vertical line from (50,10) to (50,90).
    // axis_v = (-1,0), half_w = 2.
    // Corners: (48,10), (52,10), (48,90), (52,90).
    // -> From_ltrb(48, 10, 53, 91)
    Annotation const line = Make_line(1, {50, 10}, {50, 90}, 4);

    EXPECT_EQ(Annotation_visual_bounds(line), (RectPx::From_ltrb(48, 10, 53, 91)));
}
