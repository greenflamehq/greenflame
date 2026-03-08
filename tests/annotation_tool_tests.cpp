#include "greenflame_core/freehand_annotation_tool.h"
#include "greenflame_core/line_annotation_tool.h"
#include "greenflame_core/undo_stack.h"

using namespace greenflame::core;

namespace {

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

    void Commit_new_annotation(UndoStack &undo_stack, Annotation annotation) override {
        (void)undo_stack;
        committed_annotations.push_back(std::move(annotation));
        next_annotation_id = committed_annotations.back().id + 1;
    }

    StrokeStyle stroke_style = {};
    uint64_t next_annotation_id = 1;
    std::optional<std::vector<PointPx>> smoothed_points_override = std::nullopt;
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
    EXPECT_EQ(tool.Draft_annotation(host)->kind, AnnotationKind::Freehand);
    EXPECT_EQ(tool.Draft_annotation(host)->id, 42u);
    EXPECT_EQ(tool.Draft_annotation(host)->freehand.style, host.stroke_style);
    EXPECT_EQ(tool.Draft_freehand_points().size(), 2u);
    EXPECT_EQ(tool.Draft_freehand_style(host),
              std::optional<StrokeStyle>{host.stroke_style});

    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_EQ(tool.Draft_annotation(host), nullptr);
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    EXPECT_EQ(host.committed_annotations[0].kind, AnnotationKind::Freehand);
    EXPECT_EQ(host.committed_annotations[0].id, 42u);
    EXPECT_EQ(host.committed_annotations[0].freehand.style, host.stroke_style);
    EXPECT_EQ(host.committed_annotations[0].freehand.points,
              *host.smoothed_points_override);
}

TEST(annotation_tool, FreehandTool_RefreshesDraftAfterStyleChangeNotification) {
    RecordingAnnotationToolHost host;
    FreehandAnnotationTool tool;

    host.stroke_style = StrokeStyle{4, RGB(0x10, 0x20, 0x30)};
    EXPECT_TRUE(tool.On_primary_press(host, {10, 10}));
    EXPECT_TRUE(tool.On_pointer_move(host, {20, 20}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->freehand.style.width_px, 4);

    host.stroke_style = StrokeStyle{9, RGB(0xAA, 0xBB, 0xCC)};
    tool.On_stroke_style_changed();

    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->freehand.style, host.stroke_style);
}

TEST(annotation_tool, LineTool_UsesHostStyleAngleAndCommit) {
    RecordingAnnotationToolHost host;
    UndoStack undo_stack;
    LineAnnotationTool tool;

    host.next_annotation_id = 7;
    host.stroke_style = StrokeStyle{11, RGB(0x22, 0x44, 0x66)};

    EXPECT_TRUE(tool.On_primary_press(host, {15, 20}));
    EXPECT_TRUE(tool.On_pointer_move(host, {45, 60}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->kind, AnnotationKind::Line);
    EXPECT_EQ(tool.Draft_annotation(host)->id, 7u);
    EXPECT_EQ(tool.Draft_annotation(host)->line.style, host.stroke_style);
    EXPECT_EQ(tool.Draft_annotation(host)->line.start, (PointPx{15, 20}));
    EXPECT_EQ(tool.Draft_annotation(host)->line.end, (PointPx{45, 60}));
    ASSERT_TRUE(tool.Draft_line_angle_radians().has_value());
    EXPECT_NEAR(*tool.Draft_line_angle_radians(), 0.927295218, 1e-6);

    EXPECT_TRUE(tool.On_primary_release(host, undo_stack));
    EXPECT_FALSE(tool.Has_active_gesture());
    EXPECT_EQ(tool.Draft_annotation(host), nullptr);
    ASSERT_EQ(host.committed_annotations.size(), 1u);
    EXPECT_EQ(host.committed_annotations[0].kind, AnnotationKind::Line);
    EXPECT_EQ(host.committed_annotations[0].id, 7u);
    EXPECT_EQ(host.committed_annotations[0].line.style, host.stroke_style);
    EXPECT_EQ(host.committed_annotations[0].line.start, (PointPx{15, 20}));
    EXPECT_EQ(host.committed_annotations[0].line.end, (PointPx{45, 60}));
}

TEST(annotation_tool, LineTool_RefreshesDraftAfterStyleChangeNotification) {
    RecordingAnnotationToolHost host;
    LineAnnotationTool tool;

    host.stroke_style = StrokeStyle{3, RGB(0x01, 0x02, 0x03)};
    EXPECT_TRUE(tool.On_primary_press(host, {30, 30}));
    EXPECT_TRUE(tool.On_pointer_move(host, {60, 30}));
    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->line.style.width_px, 3);

    host.stroke_style = StrokeStyle{12, RGB(0x10, 0x20, 0x30)};
    tool.On_stroke_style_changed();

    ASSERT_NE(tool.Draft_annotation(host), nullptr);
    EXPECT_EQ(tool.Draft_annotation(host)->line.style, host.stroke_style);
}
