#include "greenflame_core/bubble_annotation_tool.h"
#include "greenflame_core/ellipse_annotation_tool.h"
#include "greenflame_core/freehand_annotation_tool.h"
#include "greenflame_core/line_annotation_tool.h"
#include "greenflame_core/rectangle_annotation_tool.h"
#include "greenflame_core/text_annotation_tool.h"
#include "greenflame_core/undo_stack.h"

using namespace greenflame::core;

namespace {

[[nodiscard]] AnnotationToolDescriptor Line_tool_descriptor() {
    return AnnotationToolDescriptor{AnnotationToolId::Line, L"Line tool", L'L', L"L",
                                    AnnotationToolbarGlyph::Line};
}

[[nodiscard]] AnnotationToolDescriptor Arrow_tool_descriptor() {
    return AnnotationToolDescriptor{AnnotationToolId::Arrow, L"Arrow tool", L'A', L"A",
                                    AnnotationToolbarGlyph::Arrow};
}

[[nodiscard]] AnnotationToolDescriptor Rectangle_tool_descriptor() {
    return AnnotationToolDescriptor{AnnotationToolId::Rectangle, L"Rectangle tool",
                                    L'R', L"R", AnnotationToolbarGlyph::Rectangle};
}

[[nodiscard]] AnnotationToolDescriptor Filled_rectangle_tool_descriptor() {
    return AnnotationToolDescriptor{AnnotationToolId::FilledRectangle,
                                    L"Filled rectangle tool", L'F', L"F",
                                    AnnotationToolbarGlyph::FilledRectangle};
}

[[nodiscard]] AnnotationToolDescriptor Ellipse_tool_descriptor() {
    return AnnotationToolDescriptor{AnnotationToolId::Ellipse, L"Ellipse tool", L'E',
                                    L"E", AnnotationToolbarGlyph::Ellipse};
}

[[nodiscard]] AnnotationToolDescriptor Filled_ellipse_tool_descriptor() {
    return AnnotationToolDescriptor{AnnotationToolId::FilledEllipse,
                                    L"Filled ellipse tool", L'G', L"G",
                                    AnnotationToolbarGlyph::FilledEllipse};
}

class RecordingAnnotationToolHost final : public IAnnotationToolHost {
  public:
    [[nodiscard]] StrokeStyle Current_stroke_style() const noexcept override {
        return stroke_style;
    }

    [[nodiscard]] uint64_t Next_annotation_id() const noexcept override {
        return next_annotation_id;
    }

    [[nodiscard]] std::vector<PointPx>
    Smooth_points(std::span<const PointPx> points) const override {
        if (smoothed_points_override.has_value()) {
            return *smoothed_points_override;
        }
        return {points.begin(), points.end()};
    }

    [[nodiscard]] std::optional<Annotation>
    Build_bubble_annotation(PointPx cursor) const override {
        if (!bubble_build_enabled) {
            return std::nullopt;
        }

        Annotation annotation{};
        annotation.id = next_annotation_id;
        annotation.data = BubbleAnnotation{
            .center = cursor,
            .diameter_px = stroke_style.width_px,
            .color = stroke_style.color,
            .font_choice = bubble_font_choice,
            .counter_value = bubble_counter_value,
            .bitmap_width_px = stroke_style.width_px,
            .bitmap_height_px = stroke_style.width_px,
            .bitmap_row_bytes = stroke_style.width_px * 4,
            .premultiplied_bgra = std::vector<uint8_t>(
                static_cast<size_t>(stroke_style.width_px) *
                    static_cast<size_t>(stroke_style.width_px) * 4u,
                0xFF),
        };
        return annotation;
    }

    void Commit_new_annotation(UndoStack &undo_stack, Annotation annotation) override {
        (void)undo_stack;
        committed_annotations.push_back(std::move(annotation));
        next_annotation_id = committed_annotations.back().id + 1;
    }

    StrokeStyle stroke_style = {};
    uint64_t next_annotation_id = 1;
    std::optional<std::vector<PointPx>> smoothed_points_override = std::nullopt;
    bool bubble_build_enabled = true;
    int32_t bubble_counter_value = 1;
    TextFontChoice bubble_font_choice = TextFontChoice::Sans;
    std::vector<Annotation> committed_annotations = {};
};

} // namespace

TEST(annotation_tool, FreehandTool_UsesHostStyleSmoothingAndCommit) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    FreehandAnnotationTool tool;

    host.next_annotation_id = 42;
    host.stroke_style = StrokeStyle{7, RGB(0x12, 0x34, 0x56)};
    host.smoothed_points_override = std::vector<PointPx>{{10, 10}, {40, 25}};

    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_TRUE(tool.On_primary_press(host, {10, 10}));
    EXPECT_TRUE(tool.Has_active_gesture());
    EXPECT_TRUE(tool.On_pointer_move(host, {20, 18}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->Kind(), AnnotationKind::Freehand);
    EXPECT_EQ(tool.Draft_annotation(host)->id, 42u);
    EXPECT_EQ(
        std::get<FreehandStrokeAnnotation>(tool.Draft_annotation(host)->data).style,
        host.stroke_style);
    EXPECT_EQ(tool.Draft_freehand_points().size(), 2u);
    EXPECT_EQ(tool.Draft_freehand_style(host),
              std::optional<StrokeStyle>{host.stroke_style});

    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_EQ(tool.Draft_annotation(host), nullptr);
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    EXPECT_EQ(host.committed_annotations[0].Kind(), AnnotationKind::Freehand);
    EXPECT_EQ(host.committed_annotations[0].id, 42u);
    {
        auto const &fh =
            std::get<FreehandStrokeAnnotation>(host.committed_annotations[0].data);
        EXPECT_EQ(fh.style, host.stroke_style);
        EXPECT_EQ(fh.points, *host.smoothed_points_override);
    }
}

TEST(annotation_tool, FreehandTool_RefreshesDraftAfterStyleChangeNotification) {
    RecordingAnnotationToolHost host;
    FreehandAnnotationTool tool;

    host.stroke_style = StrokeStyle{4, RGB(0x10, 0x20, 0x30)};
    EXPECT_TRUE(tool.On_primary_press(host, {10, 10}));
    EXPECT_TRUE(tool.On_pointer_move(host, {20, 20}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(std::get<FreehandStrokeAnnotation>(tool.Draft_annotation(host)->data)
                  .style.width_px,
              4);

    host.stroke_style = StrokeStyle{9, RGB(0xAA, 0xBB, 0xCC)};
    tool.On_stroke_style_changed();

    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(
        std::get<FreehandStrokeAnnotation>(tool.Draft_annotation(host)->data).style,
        host.stroke_style);
}

TEST(annotation_tool, LineTool_UsesHostStyleAndCommit) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    LineAnnotationTool tool(Line_tool_descriptor(), false);

    host.next_annotation_id = 7;
    host.stroke_style = StrokeStyle{11, RGB(0x22, 0x44, 0x66)};

    EXPECT_TRUE(tool.On_primary_press(host, {15, 20}));
    EXPECT_TRUE(tool.On_pointer_move(host, {45, 60}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->Kind(), AnnotationKind::Line);
    EXPECT_EQ(tool.Draft_annotation(host)->id, 7u);
    {
        auto const &draft_line =
            std::get<LineAnnotation>(tool.Draft_annotation(host)->data);
        EXPECT_FALSE(draft_line.arrow_head);
        EXPECT_EQ(draft_line.style, host.stroke_style);
        EXPECT_EQ(draft_line.start, (PointPx{15, 20}));
        EXPECT_EQ(draft_line.end, (PointPx{45, 60}));
    }
    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_EQ(tool.Draft_annotation(host), nullptr);
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    EXPECT_EQ(host.committed_annotations[0].Kind(), AnnotationKind::Line);
    EXPECT_EQ(host.committed_annotations[0].id, 7u);
    {
        auto const &committed_line =
            std::get<LineAnnotation>(host.committed_annotations[0].data);
        EXPECT_FALSE(committed_line.arrow_head);
        EXPECT_EQ(committed_line.style, host.stroke_style);
        EXPECT_EQ(committed_line.start, (PointPx{15, 20}));
        EXPECT_EQ(committed_line.end, (PointPx{45, 60}));
    }
}

TEST(annotation_tool, LineTool_ClickWithoutDragDoesNotCommit) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    LineAnnotationTool tool(Line_tool_descriptor(), false);

    EXPECT_TRUE(tool.On_primary_press(host, {15, 20}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_FALSE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_EQ(tool.Draft_annotation(host), nullptr);
    EXPECT_TRUE(host.committed_annotations.empty());
}

TEST(annotation_tool, LineTool_RefreshesDraftAfterStyleChangeNotification) {
    RecordingAnnotationToolHost host;
    LineAnnotationTool tool(Line_tool_descriptor(), false);

    host.stroke_style = StrokeStyle{3, RGB(0x01, 0x02, 0x03)};
    EXPECT_TRUE(tool.On_primary_press(host, {30, 30}));
    EXPECT_TRUE(tool.On_pointer_move(host, {60, 30}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(
        std::get<LineAnnotation>(tool.Draft_annotation(host)->data).style.width_px, 3);

    host.stroke_style = StrokeStyle{12, RGB(0x10, 0x20, 0x30)};
    tool.On_stroke_style_changed();

    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(std::get<LineAnnotation>(tool.Draft_annotation(host)->data).style,
              host.stroke_style);
}

TEST(annotation_tool, ArrowTool_UsesHostStyleAndCommit) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    LineAnnotationTool tool(Arrow_tool_descriptor(), true);

    host.next_annotation_id = 13;
    host.stroke_style = StrokeStyle{5, RGB(0x22, 0x88, 0x44)};

    EXPECT_TRUE(tool.On_primary_press(host, {20, 30}));
    EXPECT_TRUE(tool.On_pointer_move(host, {60, 45}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->Kind(), AnnotationKind::Line);
    {
        auto const &draft_arrow =
            std::get<LineAnnotation>(tool.Draft_annotation(host)->data);
        EXPECT_TRUE(draft_arrow.arrow_head);
        EXPECT_EQ(draft_arrow.style, host.stroke_style);
        EXPECT_EQ(draft_arrow.start, (PointPx{20, 30}));
        EXPECT_EQ(draft_arrow.end, (PointPx{60, 45}));
    }

    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    {
        auto const &committed_arrow =
            std::get<LineAnnotation>(host.committed_annotations[0].data);
        EXPECT_TRUE(committed_arrow.arrow_head);
        EXPECT_EQ(committed_arrow.style, host.stroke_style);
    }
}

TEST(annotation_tool, RectangleTool_UsesHostStyleAndCommit) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    RectangleAnnotationTool tool(Rectangle_tool_descriptor(), false);

    host.next_annotation_id = 11;
    host.stroke_style = StrokeStyle{6, RGB(0x20, 0x40, 0x60)};

    EXPECT_TRUE(tool.On_primary_press(host, {15, 20}));
    EXPECT_TRUE(tool.On_pointer_move(host, {45, 60}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->Kind(), AnnotationKind::Rectangle);
    EXPECT_EQ(tool.Draft_annotation(host)->id, 11u);
    {
        auto const &draft_rect =
            std::get<RectangleAnnotation>(tool.Draft_annotation(host)->data);
        EXPECT_FALSE(draft_rect.filled);
        EXPECT_EQ(draft_rect.style, host.stroke_style);
        EXPECT_EQ(draft_rect.outer_bounds, (RectPx::From_ltrb(15, 20, 46, 61)));
    }

    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_EQ(tool.Draft_annotation(host), nullptr);
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    EXPECT_EQ(host.committed_annotations[0].Kind(), AnnotationKind::Rectangle);
    {
        auto const &committed_rect =
            std::get<RectangleAnnotation>(host.committed_annotations[0].data);
        EXPECT_FALSE(committed_rect.filled);
        EXPECT_EQ(committed_rect.style, host.stroke_style);
        EXPECT_EQ(committed_rect.outer_bounds, (RectPx::From_ltrb(15, 20, 46, 61)));
    }
}

TEST(annotation_tool, RectangleTool_ClickWithoutDragDoesNotCommit) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    RectangleAnnotationTool tool(Rectangle_tool_descriptor(), false);

    EXPECT_TRUE(tool.On_primary_press(host, {15, 20}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_FALSE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_EQ(tool.Draft_annotation(host), nullptr);
    EXPECT_TRUE(host.committed_annotations.empty());
}

TEST(annotation_tool, RectangleTool_RefreshesDraftAfterStyleChangeNotification) {
    RecordingAnnotationToolHost host;
    RectangleAnnotationTool tool(Rectangle_tool_descriptor(), false);

    host.stroke_style = StrokeStyle{3, RGB(0x01, 0x02, 0x03)};
    EXPECT_TRUE(tool.On_primary_press(host, {30, 30}));
    EXPECT_TRUE(tool.On_pointer_move(host, {60, 30}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(
        std::get<RectangleAnnotation>(tool.Draft_annotation(host)->data).style.width_px,
        3);

    host.stroke_style = StrokeStyle{12, RGB(0x10, 0x20, 0x30)};
    tool.On_stroke_style_changed();

    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(std::get<RectangleAnnotation>(tool.Draft_annotation(host)->data).style,
              host.stroke_style);
}

TEST(annotation_tool, FilledRectangleTool_UsesCurrentColorAndCommit) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    RectangleAnnotationTool tool(Filled_rectangle_tool_descriptor(), true);

    host.next_annotation_id = 17;
    host.stroke_style = StrokeStyle{9, RGB(0xAA, 0x44, 0x11)};

    EXPECT_TRUE(tool.On_primary_press(host, {50, 40}));
    EXPECT_TRUE(tool.On_pointer_move(host, {70, 60}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->Kind(), AnnotationKind::Rectangle);
    {
        auto const &draft_rect =
            std::get<RectangleAnnotation>(tool.Draft_annotation(host)->data);
        EXPECT_TRUE(draft_rect.filled);
        EXPECT_EQ(draft_rect.style.color, host.stroke_style.color);
        EXPECT_EQ(draft_rect.outer_bounds, (RectPx::From_ltrb(50, 40, 71, 61)));
    }

    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    {
        auto const &committed_rect =
            std::get<RectangleAnnotation>(host.committed_annotations[0].data);
        EXPECT_TRUE(committed_rect.filled);
        EXPECT_EQ(committed_rect.style.color, host.stroke_style.color);
    }
}

TEST(annotation_tool, EllipseTool_UsesHostStyleAndCommit) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    EllipseAnnotationTool tool(Ellipse_tool_descriptor(), false);

    host.next_annotation_id = 29;
    host.stroke_style = StrokeStyle{6, RGB(0x20, 0x40, 0x60)};

    EXPECT_TRUE(tool.On_primary_press(host, {15, 20}));
    EXPECT_TRUE(tool.On_pointer_move(host, {45, 60}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->Kind(), AnnotationKind::Ellipse);
    EXPECT_EQ(tool.Draft_annotation(host)->id, 29u);
    {
        auto const &draft_ellipse =
            std::get<EllipseAnnotation>(tool.Draft_annotation(host)->data);
        EXPECT_FALSE(draft_ellipse.filled);
        EXPECT_EQ(draft_ellipse.style, host.stroke_style);
        EXPECT_EQ(draft_ellipse.outer_bounds, (RectPx::From_ltrb(15, 20, 46, 61)));
    }

    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    EXPECT_EQ(host.committed_annotations[0].Kind(), AnnotationKind::Ellipse);
    {
        auto const &committed_ellipse =
            std::get<EllipseAnnotation>(host.committed_annotations[0].data);
        EXPECT_FALSE(committed_ellipse.filled);
        EXPECT_EQ(committed_ellipse.style, host.stroke_style);
    }
}

TEST(annotation_tool, FilledEllipseTool_UsesCurrentColorAndCommit) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    EllipseAnnotationTool tool(Filled_ellipse_tool_descriptor(), true);

    host.next_annotation_id = 31;
    host.stroke_style = StrokeStyle{9, RGB(0xAA, 0x44, 0x11)};

    EXPECT_TRUE(tool.On_primary_press(host, {50, 40}));
    EXPECT_TRUE(tool.On_pointer_move(host, {70, 60}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->Kind(), AnnotationKind::Ellipse);
    {
        auto const &draft_ellipse =
            std::get<EllipseAnnotation>(tool.Draft_annotation(host)->data);
        EXPECT_TRUE(draft_ellipse.filled);
        EXPECT_EQ(draft_ellipse.style.color, host.stroke_style.color);
        EXPECT_EQ(draft_ellipse.outer_bounds, (RectPx::From_ltrb(50, 40, 71, 61)));
    }

    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    {
        auto const &committed_ellipse =
            std::get<EllipseAnnotation>(host.committed_annotations[0].data);
        EXPECT_TRUE(committed_ellipse.filled);
        EXPECT_EQ(committed_ellipse.style.color, host.stroke_style.color);
    }
}

TEST(annotation_tool, TextTool_DescriptorAndNoGestureBehavior) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    TextAnnotationTool tool;

    EXPECT_EQ(tool.Descriptor().id, AnnotationToolId::Text);
    EXPECT_EQ(tool.Descriptor().name, L"Text tool");
    EXPECT_EQ(tool.Descriptor().hotkey, L'T');
    EXPECT_EQ(tool.Descriptor().toolbar_label, L"T");
    EXPECT_EQ(tool.Descriptor().toolbar_glyph, AnnotationToolbarGlyph::Text);
    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_FALSE(tool.On_primary_press(host, {10, 10}));
    EXPECT_FALSE(tool.On_pointer_move(host, {20, 20}));
    EXPECT_FALSE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.On_cancel(host));
}

TEST(annotation_tool, BubbleTool_ShowsSingleDraftAndCommitsAtLatestCursor) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    BubbleAnnotationTool tool;

    host.next_annotation_id = 23;
    host.stroke_style = StrokeStyle{12, RGB(0x11, 0x22, 0x33)};
    host.bubble_counter_value = 5;
    host.bubble_font_choice = TextFontChoice::Art;

    EXPECT_TRUE(tool.On_primary_press(host, {30, 40}));
    EXPECT_TRUE(tool.Has_active_gesture());
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->Kind(), AnnotationKind::Bubble);
    EXPECT_EQ(tool.Draft_annotation(host)->id, 23u);
    {
        auto const &draft_bubble =
            std::get<BubbleAnnotation>(tool.Draft_annotation(host)->data);
        EXPECT_EQ(draft_bubble.center, (PointPx{30, 40}));
        EXPECT_EQ(draft_bubble.diameter_px, 12);
        EXPECT_EQ(draft_bubble.color, host.stroke_style.color);
        EXPECT_EQ(draft_bubble.font_choice, TextFontChoice::Art);
        EXPECT_EQ(draft_bubble.counter_value, 5);
    }
    EXPECT_TRUE(host.committed_annotations.empty());

    EXPECT_TRUE(tool.On_pointer_move(host, {60, 70}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(std::get<BubbleAnnotation>(tool.Draft_annotation(host)->data).center,
              (PointPx{60, 70}));
    EXPECT_TRUE(host.committed_annotations.empty());

    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_EQ(tool.Draft_annotation(host), nullptr);
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    EXPECT_EQ(host.committed_annotations[0].Kind(), AnnotationKind::Bubble);
    EXPECT_EQ(host.committed_annotations[0].id, 23u);
    EXPECT_EQ(std::get<BubbleAnnotation>(host.committed_annotations[0].data).center,
              (PointPx{60, 70}));
}

TEST(annotation_tool, FreehandTool_Straighten_CollapsesFreehandToTwoPoints) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    FreehandAnnotationTool tool(AnnotationToolDescriptor{}, FreehandTipShape::Square);

    EXPECT_TRUE(tool.On_primary_press(host, {10, 20}));
    EXPECT_TRUE(tool.On_pointer_move(host, {30, 40}));
    EXPECT_TRUE(tool.On_pointer_move(host, {50, 60}));
    EXPECT_EQ(tool.Draft_freehand_points().size(), 3u);

    tool.Straighten();

    EXPECT_EQ(tool.Draft_freehand_points().size(), 2u);
    EXPECT_EQ(tool.Draft_freehand_points()[0], (PointPx{10, 20}));
    EXPECT_EQ(tool.Draft_freehand_points()[1], (PointPx{50, 60}));
}

TEST(annotation_tool, FreehandTool_Straighten_EndTracksMouseAfterStraighten) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    FreehandAnnotationTool tool(AnnotationToolDescriptor{}, FreehandTipShape::Square);

    EXPECT_TRUE(tool.On_primary_press(host, {10, 20}));
    EXPECT_TRUE(tool.On_pointer_move(host, {30, 40}));
    tool.Straighten();
    EXPECT_TRUE(tool.On_pointer_move(host, {70, 80}));

    EXPECT_EQ(tool.Draft_freehand_points().size(), 2u);
    EXPECT_EQ(tool.Draft_freehand_points()[0], (PointPx{10, 20}));
    EXPECT_EQ(tool.Draft_freehand_points()[1], (PointPx{70, 80}));
}

TEST(annotation_tool, FreehandTool_Straighten_CommitsWithTwoPointsNoSmoothing) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    FreehandAnnotationTool tool(AnnotationToolDescriptor{}, FreehandTipShape::Square);

    host.smoothed_points_override = std::vector<PointPx>{{0, 0}, {1, 1}, {2, 2}};

    EXPECT_TRUE(tool.On_primary_press(host, {10, 20}));
    EXPECT_TRUE(tool.On_pointer_move(host, {50, 60}));
    tool.Straighten();

    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    auto const &fh =
        std::get<FreehandStrokeAnnotation>(host.committed_annotations[0].data);
    EXPECT_EQ(fh.points.size(), 2u);
    EXPECT_EQ(fh.points[0], (PointPx{10, 20}));
    EXPECT_EQ(fh.points[1], (PointPx{50, 60}));
}

TEST(annotation_tool, FreehandTool_Straighten_ResetClearsStraightenedState) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    FreehandAnnotationTool tool(AnnotationToolDescriptor{}, FreehandTipShape::Square);

    EXPECT_TRUE(tool.On_primary_press(host, {10, 20}));
    EXPECT_TRUE(tool.On_pointer_move(host, {30, 40}));
    tool.Straighten();
    tool.Reset();

    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_TRUE(tool.On_primary_press(host, {10, 20}));
    EXPECT_TRUE(tool.On_pointer_move(host, {30, 40}));
    EXPECT_TRUE(tool.On_pointer_move(host, {50, 60}));
    EXPECT_EQ(tool.Draft_freehand_points().size(), 3u);
}
