#include "fake_text_layout_engine.h"
#include "greenflame_core/annotation_controller.h"
#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/undo_stack.h"

using namespace greenflame::core;

namespace {

Annotation Make_stroke(uint64_t id, std::initializer_list<PointPx> points) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = FreehandStrokeAnnotation{
        .points = std::vector<PointPx>(points),
        .style = {},
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
                     std::wstring text = L"abc") {
    Annotation annotation{};
    annotation.id = id;

    TextAnnotation text_annotation{};
    text_annotation.origin = origin;
    text_annotation.base_style = {
        .color = RGB(0x11, 0x22, 0x33),
        .font_choice = TextFontChoice::Sans,
        .point_size = 12,
    };
    text_annotation.runs = {TextRun{std::move(text), {}}};
    text_annotation.visual_bounds = visual_bounds;
    text_annotation.bitmap_width_px = visual_bounds.Width();
    text_annotation.bitmap_height_px = visual_bounds.Height();
    text_annotation.bitmap_row_bytes = text_annotation.bitmap_width_px * 4;
    text_annotation.premultiplied_bgra.assign(
        static_cast<size_t>(text_annotation.bitmap_row_bytes) *
            static_cast<size_t>(text_annotation.bitmap_height_px),
        0);
    for (size_t index = 3; index < text_annotation.premultiplied_bgra.size();
         index += 4) {
        text_annotation.premultiplied_bgra[index] = 255;
    }

    annotation.data = std::move(text_annotation);
    return annotation;
}

} // namespace

TEST(annotation_controller, InitialState_DefaultsToNoActiveTool) {
    AnnotationController controller;

    EXPECT_EQ(controller.Active_tool(), std::nullopt);
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
}

TEST(annotation_controller, ToolbarViews_ExposeAnnotationTools) {
    AnnotationController controller;

    std::vector<AnnotationToolbarButtonView> const views =
        controller.Build_toolbar_button_views();

    ASSERT_EQ(views.size(), 10u);
    EXPECT_EQ(views[0].id, AnnotationToolId::Freehand);
    EXPECT_EQ(views[0].label, L"B");
    EXPECT_EQ(views[0].tooltip, L"Brush (B)");
    EXPECT_EQ(views[0].glyph, AnnotationToolbarGlyph::Brush);
    EXPECT_FALSE(views[0].active);
    EXPECT_EQ(views[1].id, AnnotationToolId::Highlighter);
    EXPECT_EQ(views[1].label, L"H");
    EXPECT_EQ(views[1].tooltip, L"Highlighter (H)");
    EXPECT_EQ(views[1].glyph, AnnotationToolbarGlyph::Highlighter);
    EXPECT_FALSE(views[1].active);
    EXPECT_EQ(views[2].id, AnnotationToolId::Line);
    EXPECT_EQ(views[2].label, L"L");
    EXPECT_EQ(views[2].tooltip, L"Line (L)");
    EXPECT_EQ(views[2].glyph, AnnotationToolbarGlyph::Line);
    EXPECT_FALSE(views[2].active);
    EXPECT_EQ(views[3].id, AnnotationToolId::Arrow);
    EXPECT_EQ(views[3].label, L"A");
    EXPECT_EQ(views[3].tooltip, L"Arrow (A)");
    EXPECT_EQ(views[3].glyph, AnnotationToolbarGlyph::Arrow);
    EXPECT_FALSE(views[3].active);
    EXPECT_EQ(views[4].id, AnnotationToolId::Rectangle);
    EXPECT_EQ(views[4].label, L"R");
    EXPECT_EQ(views[4].tooltip, L"Rectangle (R)");
    EXPECT_EQ(views[4].glyph, AnnotationToolbarGlyph::Rectangle);
    EXPECT_FALSE(views[4].active);
    EXPECT_EQ(views[5].id, AnnotationToolId::FilledRectangle);
    EXPECT_EQ(views[5].label, L"\u21e7R");
    EXPECT_EQ(views[5].tooltip, L"Filled rectangle (Shift+R)");
    EXPECT_EQ(views[5].glyph, AnnotationToolbarGlyph::FilledRectangle);
    EXPECT_FALSE(views[5].active);
    EXPECT_EQ(views[6].id, AnnotationToolId::Ellipse);
    EXPECT_EQ(views[6].label, L"E");
    EXPECT_EQ(views[6].tooltip, L"Ellipse (E)");
    EXPECT_EQ(views[6].glyph, AnnotationToolbarGlyph::Ellipse);
    EXPECT_FALSE(views[6].active);
    EXPECT_EQ(views[7].id, AnnotationToolId::FilledEllipse);
    EXPECT_EQ(views[7].label, L"\u21e7E");
    EXPECT_EQ(views[7].tooltip, L"Filled ellipse (Shift+E)");
    EXPECT_EQ(views[7].glyph, AnnotationToolbarGlyph::FilledEllipse);
    EXPECT_FALSE(views[7].active);
    EXPECT_EQ(views[8].id, AnnotationToolId::Text);
    EXPECT_EQ(views[8].label, L"T");
    EXPECT_EQ(views[8].tooltip, L"Text (T)");
    EXPECT_EQ(views[8].glyph, AnnotationToolbarGlyph::Text);
    EXPECT_FALSE(views[8].active);
    EXPECT_EQ(views[9].id, AnnotationToolId::Bubble);
    EXPECT_EQ(views[9].label, L"N");
    EXPECT_EQ(views[9].tooltip, L"Bubble (N)");
    EXPECT_EQ(views[9].glyph, AnnotationToolbarGlyph::Bubble);
    EXPECT_FALSE(views[9].active);
}

TEST(annotation_controller, ToggleToolByHotkey_ActivatesAndDeactivatesFreehand) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'B'));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Freehand});
    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'B'));
    EXPECT_EQ(controller.Active_tool(), std::nullopt);
}

TEST(annotation_controller, ToggleToolByLowercaseHotkey_ActivatesFreehand) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'b'));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Freehand});
}

TEST(annotation_controller, ToggleToolByHotkey_ActivatesAndDeactivatesHighlighter) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'H'));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Highlighter});
    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'h'));
    EXPECT_EQ(controller.Active_tool(), std::nullopt);
}

TEST(annotation_controller, ToggleToolByHotkey_ActivatesAndDeactivatesLine) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'L'));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Line});
    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'l'));
    EXPECT_EQ(controller.Active_tool(), std::nullopt);
}

TEST(annotation_controller, ToggleToolByHotkey_ActivatesAndDeactivatesArrow) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'A'));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Arrow});
    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'a'));
    EXPECT_EQ(controller.Active_tool(), std::nullopt);
}

TEST(annotation_controller, ToggleToolByHotkey_ActivatesAndDeactivatesRectangle) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'R'));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Rectangle});
    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'r'));
    EXPECT_EQ(controller.Active_tool(), std::nullopt);
}

TEST(annotation_controller, ToggleToolByHotkey_ActivatesAndDeactivatesFilledRectangle) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'R', true));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::FilledRectangle});
    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'r', true));
    EXPECT_EQ(controller.Active_tool(), std::nullopt);
}

TEST(annotation_controller, ToggleToolByHotkey_ActivatesAndDeactivatesEllipse) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'E'));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Ellipse});
    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'e'));
    EXPECT_EQ(controller.Active_tool(), std::nullopt);
}

TEST(annotation_controller, ToggleToolByHotkey_ActivatesAndDeactivatesFilledEllipse) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'E', true));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::FilledEllipse});
    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'e', true));
    EXPECT_EQ(controller.Active_tool(), std::nullopt);
}

TEST(annotation_controller, ToggleToolByHotkey_ActivatesAndDeactivatesText) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'T'));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Text});
    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L't'));
    EXPECT_EQ(controller.Active_tool(), std::nullopt);
}

TEST(annotation_controller, BeginTextDraft_CapturesCurrentColorFontAndPointSize) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    COLORREF const green = RGB(0x12, 0xA4, 0x56);

    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Set_annotation_color(green));
    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Text, 12));
    controller.Set_text_current_font(TextFontChoice::Mono);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Text));

    EXPECT_TRUE(controller.On_primary_press({50, 60}));
    ASSERT_TRUE(controller.Has_active_text_edit());

    TextDraftView const view = controller.Active_text_edit()->Build_view();
    ASSERT_NE(view.annotation, nullptr);
    EXPECT_EQ(view.annotation->origin, (PointPx{61, 70}));
    EXPECT_EQ(view.annotation->base_style.color, green);
    EXPECT_EQ(view.annotation->base_style.font_choice, TextFontChoice::Mono);
    EXPECT_EQ(view.annotation->base_style.point_size, 16);
    EXPECT_TRUE(view.annotation->runs.empty());
    EXPECT_TRUE(view.insert_mode);

    controller.Active_text_edit()->On_text_input(L"x");
    TextDraftView const typed_view = controller.Active_text_edit()->Build_view();
    ASSERT_NE(typed_view.annotation, nullptr);
    ASSERT_EQ(typed_view.annotation->runs.size(), 1u);
    EXPECT_EQ(typed_view.annotation->runs[0].text, L"x");
    EXPECT_FALSE(typed_view.annotation->runs[0].flags.bold);
    EXPECT_FALSE(typed_view.annotation->runs[0].flags.italic);
    EXPECT_FALSE(typed_view.annotation->runs[0].flags.underline);
    EXPECT_FALSE(typed_view.annotation->runs[0].flags.strikethrough);
}

TEST(annotation_controller, CommitTextAnnotation_AddsUndoableTextAnnotation) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    UndoStack undo_stack;

    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Text));
    EXPECT_TRUE(controller.On_primary_press({20, 30}));
    ASSERT_TRUE(controller.Has_active_text_edit());

    controller.Active_text_edit()->On_text_input(L"abc");
    EXPECT_EQ(undo_stack.Count(), 0u);
    EXPECT_EQ(undo_stack.Index(), 0);
    controller.Commit_text_annotation(undo_stack,
                                      controller.Active_text_edit()->Commit());

    EXPECT_FALSE(controller.Has_active_text_edit());
    EXPECT_EQ(undo_stack.Count(), 1u);
    EXPECT_EQ(undo_stack.Index(), 1);
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0].Kind(), AnnotationKind::Text);
    {
        auto const &text = std::get<TextAnnotation>(controller.Annotations()[0].data);
        EXPECT_EQ(Flatten_text(text.runs), L"abc");
        EXPECT_EQ(text.origin, (PointPx{31, 40}));
        EXPECT_EQ(text.bitmap_width_px, text.visual_bounds.Width());
        EXPECT_EQ(text.bitmap_height_px, text.visual_bounds.Height());
    }

    undo_stack.Undo();
    EXPECT_EQ(undo_stack.Count(), 1u);
    EXPECT_EQ(undo_stack.Index(), 0);
    EXPECT_TRUE(controller.Annotations().empty());

    undo_stack.Redo();
    EXPECT_EQ(undo_stack.Count(), 1u);
    EXPECT_EQ(undo_stack.Index(), 1);
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0].Kind(), AnnotationKind::Text);
}

TEST(annotation_controller, CommitTextAnnotation_PreservesInsertedNewlines) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    UndoStack undo_stack;

    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Text));
    EXPECT_TRUE(controller.On_primary_press({20, 30}));
    ASSERT_TRUE(controller.Has_active_text_edit());

    controller.Active_text_edit()->On_text_input(L"alpha\nbeta");
    controller.Commit_text_annotation(undo_stack,
                                      controller.Active_text_edit()->Commit());

    ASSERT_EQ(controller.Annotations().size(), 1u);
    ASSERT_EQ(controller.Annotations()[0].Kind(), AnnotationKind::Text);
    EXPECT_EQ(
        Flatten_text(std::get<TextAnnotation>(controller.Annotations()[0].data).runs),
        L"alpha\nbeta");
}

TEST(annotation_controller, CancelTextDraft_ClearsDraftWithoutCommit) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    UndoStack undo_stack;

    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Text));
    EXPECT_TRUE(controller.On_primary_press({10, 20}));
    ASSERT_TRUE(controller.Has_active_text_edit());

    controller.Active_text_edit()->On_text_input(L"abc");
    EXPECT_EQ(undo_stack.Count(), 0u);
    EXPECT_EQ(undo_stack.Index(), 0);
    EXPECT_TRUE(controller.On_cancel());
    EXPECT_FALSE(controller.Has_active_text_edit());
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(undo_stack.Count(), 0u);
    EXPECT_EQ(undo_stack.Index(), 0);
}

TEST(annotation_controller, CommittedTextAnnotation_IsSelectableMovableAndDeletable) {
    AnnotationController controller;
    UndoStack undo_stack;
    Annotation const original =
        Make_text(1, {20, 30}, RectPx::From_ltrb(20, 30, 50, 50));

    controller.Insert_annotation_at(0, original, std::nullopt);

    EXPECT_TRUE(controller.Select_topmost_annotation({25, 35}));
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});

    ASSERT_TRUE(controller.Begin_annotation_edit(
        AnnotationEditTarget{1, AnnotationEditTargetKind::Body}, {25, 35}));
    EXPECT_TRUE(controller.On_pointer_move({35, 45}));
    {
        auto const &moved_text =
            std::get<TextAnnotation>(controller.Annotations()[0].data);
        EXPECT_EQ(moved_text.origin, (PointPx{30, 40}));
        EXPECT_EQ(moved_text.visual_bounds, (RectPx::From_ltrb(30, 40, 60, 60)));
    }
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    Annotation const moved = controller.Annotations()[0];

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0], original);
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});

    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0], moved);
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});

    EXPECT_TRUE(controller.Delete_selected_annotation(undo_stack));
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0], moved);
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});

    undo_stack.Redo();
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
}

TEST(annotation_controller, AnnotationIdAt_ReturnsTopmostAnnotationByCoveredPixel) {
    AnnotationController controller;
    controller.Insert_annotation_at(0, Make_stroke(1, {{20, 20}, {30, 20}}),
                                    std::nullopt);
    controller.Insert_annotation_at(1, Make_stroke(2, {{20, 20}, {30, 20}}),
                                    std::nullopt);

    EXPECT_EQ(controller.Annotation_id_at({25, 20}), std::optional<uint64_t>{2});
}

TEST(annotation_controller, SetSelectedAnnotation_ClearsSelection) {
    AnnotationController controller;
    controller.Insert_annotation_at(0, Make_stroke(1, {{20, 20}, {30, 20}}),
                                    std::optional<uint64_t>{1});

    EXPECT_TRUE(controller.Set_selected_annotation(std::nullopt));
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
    EXPECT_FALSE(controller.Set_selected_annotation(std::nullopt));
}

TEST(annotation_controller, FreehandRelease_AddsAnnotationAndKeepsSelectionEmpty) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Freehand));

    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_pointer_move({12, 11}));
    EXPECT_TRUE(controller.On_pointer_move({14, 12}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Freehand});
    {
        auto const &fh =
            std::get<FreehandStrokeAnnotation>(controller.Annotations()[0].data);
        EXPECT_EQ(fh.points.size(), 3u);
        EXPECT_EQ(fh.points[0], (PointPx{10, 10}));
        EXPECT_EQ(fh.points[2], (PointPx{14, 12}));
    }
}

TEST(annotation_controller, FreehandToolSelectionClearsSelectedAnnotation) {
    AnnotationController controller;
    UndoStack undo_stack;
    controller.Insert_annotation_at(0, Make_stroke(1, {{20, 20}, {30, 20}}),
                                    std::optional<uint64_t>{1});
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Freehand));
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);

    EXPECT_TRUE(controller.On_primary_press({100, 100}));
    EXPECT_TRUE(controller.On_pointer_move({110, 110}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 2u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);

    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 2u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
}

TEST(annotation_controller, LineRelease_AddsAnnotationAndKeepsSelectionEmpty) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Line));

    EXPECT_TRUE(controller.On_primary_press({15, 25}));
    EXPECT_TRUE(controller.On_pointer_move({45, 55}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Line});
    {
        auto const &line = std::get<LineAnnotation>(controller.Annotations()[0].data);
        EXPECT_EQ(controller.Annotations()[0].Kind(), AnnotationKind::Line);
        EXPECT_FALSE(line.arrow_head);
        EXPECT_EQ(line.start, (PointPx{15, 25}));
        EXPECT_EQ(line.end, (PointPx{45, 55}));
    }
}

TEST(annotation_controller, LineClickWithoutDrag_DoesNotCommitOrPushUndo) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Line));

    EXPECT_TRUE(controller.On_primary_press({15, 25}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_FALSE(controller.On_primary_release(undo_stack));

    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(undo_stack.Count(), 0u);
    EXPECT_EQ(undo_stack.Index(), 0);
}

TEST(annotation_controller, ArrowRelease_AddsAnnotationAndKeepsSelectionEmpty) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Arrow));

    EXPECT_TRUE(controller.On_primary_press({15, 25}));
    EXPECT_TRUE(controller.On_pointer_move({45, 55}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Arrow});
    {
        auto const &arrow = std::get<LineAnnotation>(controller.Annotations()[0].data);
        EXPECT_EQ(controller.Annotations()[0].Kind(), AnnotationKind::Line);
        EXPECT_TRUE(arrow.arrow_head);
        EXPECT_EQ(arrow.start, (PointPx{15, 25}));
        EXPECT_EQ(arrow.end, (PointPx{45, 55}));
    }
}

TEST(annotation_controller, ArrowClickWithoutDrag_DoesNotCommitOrPushUndo) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Arrow));

    EXPECT_TRUE(controller.On_primary_press({15, 25}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_FALSE(controller.On_primary_release(undo_stack));

    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(undo_stack.Count(), 0u);
    EXPECT_EQ(undo_stack.Index(), 0);
}

TEST(annotation_controller, LineToolSelectionClearsSelectedAnnotation) {
    AnnotationController controller;
    UndoStack undo_stack;
    controller.Insert_annotation_at(0, Make_stroke(1, {{20, 20}, {30, 20}}),
                                    std::optional<uint64_t>{1});
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Line));
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);

    EXPECT_TRUE(controller.On_primary_press({100, 100}));
    EXPECT_TRUE(controller.On_pointer_move({140, 130}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 2u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);

    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 2u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
}

TEST(annotation_controller, RectangleRelease_AddsAnnotationAndKeepsSelectionEmpty) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Rectangle));

    EXPECT_TRUE(controller.On_primary_press({15, 25}));
    EXPECT_TRUE(controller.On_pointer_move({45, 55}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Rectangle});
    {
        auto const &rect =
            std::get<RectangleAnnotation>(controller.Annotations()[0].data);
        EXPECT_EQ(controller.Annotations()[0].Kind(), AnnotationKind::Rectangle);
        EXPECT_FALSE(rect.filled);
        EXPECT_EQ(rect.outer_bounds, (RectPx::From_ltrb(15, 25, 46, 56)));
    }
}

TEST(annotation_controller, RectangleClickWithoutDrag_DoesNotCommitOrPushUndo) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Rectangle));

    EXPECT_TRUE(controller.On_primary_press({15, 25}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_FALSE(controller.On_primary_release(undo_stack));

    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(undo_stack.Count(), 0u);
    EXPECT_EQ(undo_stack.Index(), 0);
}

TEST(annotation_controller,
     FilledRectangleRelease_AddsAnnotationAndKeepsSelectionEmpty) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::FilledRectangle));

    EXPECT_TRUE(controller.On_primary_press({20, 30}));
    EXPECT_TRUE(controller.On_pointer_move({35, 40}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
    {
        auto const &rect =
            std::get<RectangleAnnotation>(controller.Annotations()[0].data);
        EXPECT_TRUE(rect.filled);
        EXPECT_EQ(rect.outer_bounds, (RectPx::From_ltrb(20, 30, 36, 41)));
    }
}

TEST(annotation_controller, FilledRectangleClickWithoutDrag_DoesNotCommitOrPushUndo) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::FilledRectangle));

    EXPECT_TRUE(controller.On_primary_press({15, 25}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_FALSE(controller.On_primary_release(undo_stack));

    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(undo_stack.Count(), 0u);
    EXPECT_EQ(undo_stack.Index(), 0);
}

TEST(annotation_controller, EllipseRelease_AddsAnnotationAndKeepsSelectionEmpty) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Ellipse));

    EXPECT_TRUE(controller.On_primary_press({20, 30}));
    EXPECT_TRUE(controller.On_pointer_move({35, 40}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
    {
        auto const &ellipse =
            std::get<EllipseAnnotation>(controller.Annotations()[0].data);
        EXPECT_FALSE(ellipse.filled);
        EXPECT_EQ(ellipse.outer_bounds, (RectPx::From_ltrb(20, 30, 36, 41)));
    }
}

TEST(annotation_controller, FilledEllipseRelease_AddsAnnotationAndKeepsSelectionEmpty) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::FilledEllipse));

    EXPECT_TRUE(controller.On_primary_press({20, 30}));
    EXPECT_TRUE(controller.On_pointer_move({35, 40}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
    {
        auto const &ellipse =
            std::get<EllipseAnnotation>(controller.Annotations()[0].data);
        EXPECT_TRUE(ellipse.filled);
        EXPECT_EQ(ellipse.outer_bounds, (RectPx::From_ltrb(20, 30, 36, 41)));
    }
}

TEST(annotation_controller, FreehandDraftPointsTrackActiveGesture) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Freehand));

    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_pointer_move({12, 11}));
    EXPECT_TRUE(controller.On_pointer_move({14, 12}));

    ASSERT_EQ(controller.Draft_freehand_points().size(), 3u);
    EXPECT_EQ(controller.Draft_freehand_points()[0], (PointPx{10, 10}));
    EXPECT_EQ(controller.Draft_freehand_points()[2], (PointPx{14, 12}));
    EXPECT_EQ(controller.Draft_freehand_style(),
              std::optional<StrokeStyle>{StrokeStyle{}});

    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_TRUE(controller.Draft_freehand_points().empty());
    EXPECT_EQ(controller.Draft_freehand_style(), std::nullopt);
}

TEST(annotation_controller, LineDraftTracksActiveGesture) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Line));

    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_pointer_move({20, 20}));

    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(controller.Draft_annotation()->Kind(), AnnotationKind::Line);
    {
        auto const &draft_line =
            std::get<LineAnnotation>(controller.Draft_annotation()->data);
        EXPECT_FALSE(draft_line.arrow_head);
        EXPECT_EQ(draft_line.start, (PointPx{10, 10}));
        EXPECT_EQ(draft_line.end, (PointPx{20, 20}));
    }
    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
}

TEST(annotation_controller, ArrowDraftTracksActiveGesture) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Arrow));

    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_pointer_move({20, 20}));

    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(controller.Draft_annotation()->Kind(), AnnotationKind::Line);
    {
        auto const &draft_arrow =
            std::get<LineAnnotation>(controller.Draft_annotation()->data);
        EXPECT_TRUE(draft_arrow.arrow_head);
        EXPECT_EQ(draft_arrow.start, (PointPx{10, 10}));
        EXPECT_EQ(draft_arrow.end, (PointPx{20, 20}));
    }
    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
}

TEST(annotation_controller, RectangleDraftTracksActiveGesture) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Rectangle));

    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_pointer_move({20, 20}));

    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(controller.Draft_annotation()->Kind(), AnnotationKind::Rectangle);
    {
        auto const &draft_rect =
            std::get<RectangleAnnotation>(controller.Draft_annotation()->data);
        EXPECT_FALSE(draft_rect.filled);
        EXPECT_EQ(draft_rect.outer_bounds, (RectPx::From_ltrb(10, 10, 21, 21)));
    }

    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
}

TEST(annotation_controller, EllipseDraftTracksActiveGesture) {
    AnnotationController controller;
    UndoStack undo_stack;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Ellipse));

    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_pointer_move({20, 20}));

    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(controller.Draft_annotation()->Kind(), AnnotationKind::Ellipse);
    {
        auto const &draft_ellipse =
            std::get<EllipseAnnotation>(controller.Draft_annotation()->data);
        EXPECT_FALSE(draft_ellipse.filled);
        EXPECT_EQ(draft_ellipse.outer_bounds, (RectPx::From_ltrb(10, 10, 21, 21)));
    }

    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
}

TEST(annotation_controller, ToolSize_ClampsToSupportedRange) {
    AnnotationController controller;

    // Freehand: step = physical px
    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Freehand, 0));
    EXPECT_EQ(controller.Tool_physical_size(AnnotationToolId::Freehand), 1);
    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Freehand, 500));
    EXPECT_EQ(controller.Tool_physical_size(AnnotationToolId::Freehand), 50);

    // Highlighter: physical = step + 10
    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Highlighter, 0));
    EXPECT_EQ(controller.Tool_physical_size(AnnotationToolId::Highlighter), 11);
    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Highlighter, 500));
    EXPECT_EQ(controller.Tool_physical_size(AnnotationToolId::Highlighter), 60);

    // Bubble: physical = step + 20
    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Bubble, 0));
    EXPECT_EQ(controller.Tool_physical_size(AnnotationToolId::Bubble), 21);
    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Bubble, 500));
    EXPECT_EQ(controller.Tool_physical_size(AnnotationToolId::Bubble), 70);
}

TEST(annotation_controller, ToolSize_AffectsDraftAndCommittedFreehandStyle) {
    AnnotationController controller;
    UndoStack undo_stack;

    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Freehand, 12));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Freehand));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_TRUE(controller.Draft_freehand_style().has_value());
    EXPECT_EQ(controller.Draft_freehand_style()->width_px, 12);

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(std::get<FreehandStrokeAnnotation>(controller.Annotations()[0].data)
                  .style.width_px,
              12);
}

TEST(annotation_controller, ToolSize_AffectsDraftAndCommittedLineStyle) {
    AnnotationController controller;
    UndoStack undo_stack;

    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Line, 12));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Line));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(
        std::get<LineAnnotation>(controller.Draft_annotation()->data).style.width_px,
        12);

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(std::get<LineAnnotation>(controller.Annotations()[0].data).style.width_px,
              12);
}

TEST(annotation_controller, ToolSize_AffectsDraftAndCommittedArrowStyle) {
    AnnotationController controller;
    UndoStack undo_stack;

    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Arrow, 12));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Arrow));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    {
        auto const &draft_arrow =
            std::get<LineAnnotation>(controller.Draft_annotation()->data);
        EXPECT_EQ(draft_arrow.style.width_px, 12);
        EXPECT_TRUE(draft_arrow.arrow_head);
    }

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    {
        auto const &arrow = std::get<LineAnnotation>(controller.Annotations()[0].data);
        EXPECT_EQ(arrow.style.width_px, 12);
        EXPECT_TRUE(arrow.arrow_head);
    }
}

TEST(annotation_controller, ToolSize_AffectsDraftAndCommittedHighlighterStyle) {
    AnnotationController controller;
    UndoStack undo_stack;

    // Highlighter physical = step + 10; step 2 → 12 px
    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Highlighter, 2));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Highlighter));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_TRUE(controller.Draft_freehand_style().has_value());
    EXPECT_EQ(controller.Draft_freehand_style()->width_px, 12);
    EXPECT_EQ(controller.Draft_freehand_style()->opacity_percent,
              controller.Highlighter_opacity_percent());

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    auto const &highlighter =
        std::get<FreehandStrokeAnnotation>(controller.Annotations()[0].data);
    EXPECT_EQ(highlighter.style.width_px, 12);
    EXPECT_EQ(highlighter.freehand_tip_shape, FreehandTipShape::Square);
    EXPECT_EQ(highlighter.style.opacity_percent,
              controller.Highlighter_opacity_percent());
}

TEST(annotation_controller, ToolSize_AffectsDraftAndCommittedRectangleStyle) {
    AnnotationController controller;
    UndoStack undo_stack;

    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Rectangle, 12));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Rectangle));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(std::get<RectangleAnnotation>(controller.Draft_annotation()->data)
                  .style.width_px,
              12);

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(
        std::get<RectangleAnnotation>(controller.Annotations()[0].data).style.width_px,
        12);
}

TEST(annotation_controller, ToolSize_AffectsDraftAndCommittedEllipseStyle) {
    AnnotationController controller;
    UndoStack undo_stack;

    EXPECT_TRUE(controller.Set_tool_size_step(AnnotationToolId::Ellipse, 12));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Ellipse));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(
        std::get<EllipseAnnotation>(controller.Draft_annotation()->data).style.width_px,
        12);

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(
        std::get<EllipseAnnotation>(controller.Annotations()[0].data).style.width_px,
        12);
}

TEST(annotation_controller, AnnotationColor_AffectsDraftAndCommittedStrokeStyle) {
    AnnotationController controller;
    UndoStack undo_stack;
    COLORREF const green = RGB(0x12, 0xA4, 0x56);

    EXPECT_TRUE(controller.Set_annotation_color(green));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Freehand));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_TRUE(controller.Draft_freehand_style().has_value());
    EXPECT_EQ(controller.Draft_freehand_style()->color, green);

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(std::get<FreehandStrokeAnnotation>(controller.Annotations()[0].data)
                  .style.color,
              green);
}

TEST(annotation_controller, HighlighterColor_AffectsDraftAndCommittedStrokeStyle) {
    AnnotationController controller;
    UndoStack undo_stack;
    COLORREF const green = RGB(0x12, 0xA4, 0x56);

    EXPECT_TRUE(controller.Set_highlighter_color(green));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Highlighter));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_TRUE(controller.Draft_freehand_style().has_value());
    EXPECT_EQ(controller.Draft_freehand_style()->color, green);
    EXPECT_EQ(controller.Annotation_color(), green);

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(std::get<FreehandStrokeAnnotation>(controller.Annotations()[0].data)
                  .style.color,
              green);
}

TEST(annotation_controller, SetAnnotationColor_RoutesToHighlighterWhenActive) {
    AnnotationController controller;
    COLORREF const brush_color = RGB(0x11, 0x22, 0x33);
    COLORREF const highlighter_color = RGB(0x44, 0x55, 0x66);

    EXPECT_TRUE(controller.Set_brush_annotation_color(brush_color));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Highlighter));
    EXPECT_TRUE(controller.Set_annotation_color(highlighter_color));

    EXPECT_EQ(controller.Brush_annotation_color(), brush_color);
    EXPECT_EQ(controller.Highlighter_color(), highlighter_color);
    EXPECT_EQ(controller.Annotation_color(), highlighter_color);
}

TEST(annotation_controller, AnnotationColor_AffectsDraftAndCommittedLineStyle) {
    AnnotationController controller;
    UndoStack undo_stack;
    COLORREF const green = RGB(0x12, 0xA4, 0x56);

    EXPECT_TRUE(controller.Set_annotation_color(green));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Line));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(std::get<LineAnnotation>(controller.Draft_annotation()->data).style.color,
              green);

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(std::get<LineAnnotation>(controller.Annotations()[0].data).style.color,
              green);
}

TEST(annotation_controller, AnnotationColor_AffectsDraftAndCommittedArrowStyle) {
    AnnotationController controller;
    UndoStack undo_stack;
    COLORREF const green = RGB(0x12, 0xA4, 0x56);

    EXPECT_TRUE(controller.Set_annotation_color(green));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Arrow));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    {
        auto const &draft_arrow =
            std::get<LineAnnotation>(controller.Draft_annotation()->data);
        EXPECT_EQ(draft_arrow.style.color, green);
        EXPECT_TRUE(draft_arrow.arrow_head);
    }

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    {
        auto const &arrow = std::get<LineAnnotation>(controller.Annotations()[0].data);
        EXPECT_EQ(arrow.style.color, green);
        EXPECT_TRUE(arrow.arrow_head);
    }
}

TEST(annotation_controller, AnnotationColor_AffectsDraftAndCommittedFilledRectangle) {
    AnnotationController controller;
    UndoStack undo_stack;
    COLORREF const green = RGB(0x12, 0xA4, 0x56);

    EXPECT_TRUE(controller.Set_annotation_color(green));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::FilledRectangle));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    {
        auto const &draft_rect =
            std::get<RectangleAnnotation>(controller.Draft_annotation()->data);
        EXPECT_EQ(draft_rect.style.color, green);
        EXPECT_TRUE(draft_rect.filled);
    }

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(
        std::get<RectangleAnnotation>(controller.Annotations()[0].data).style.color,
        green);
}

TEST(annotation_controller, AnnotationColor_AffectsDraftAndCommittedFilledEllipse) {
    AnnotationController controller;
    UndoStack undo_stack;
    COLORREF const green = RGB(0x12, 0xA4, 0x56);

    EXPECT_TRUE(controller.Set_annotation_color(green));
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::FilledEllipse));
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    {
        auto const &draft_ellipse =
            std::get<EllipseAnnotation>(controller.Draft_annotation()->data);
        EXPECT_EQ(draft_ellipse.style.color, green);
        EXPECT_TRUE(draft_ellipse.filled);
    }

    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(std::get<EllipseAnnotation>(controller.Annotations()[0].data).style.color,
              green);
}

TEST(annotation_controller, CancelDuringFreehand_ClearsDraftWithoutCommit) {
    AnnotationController controller;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Freehand));

    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_pointer_move({20, 10}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);

    EXPECT_TRUE(controller.On_cancel());
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
    EXPECT_TRUE(controller.Annotations().empty());
}

TEST(annotation_controller, CancelDuringLine_ClearsDraftWithoutCommit) {
    AnnotationController controller;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Line));

    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_pointer_move({25, 18}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);

    EXPECT_TRUE(controller.On_cancel());
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
    EXPECT_TRUE(controller.Annotations().empty());
}

TEST(annotation_controller, CancelDuringArrow_ClearsDraftWithoutCommit) {
    AnnotationController controller;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Arrow));

    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_pointer_move({25, 18}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);

    EXPECT_TRUE(controller.On_cancel());
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
    EXPECT_TRUE(controller.Annotations().empty());
}

TEST(annotation_controller, CancelDuringRectangle_ClearsDraftWithoutCommit) {
    AnnotationController controller;
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Rectangle));

    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_pointer_move({25, 18}));
    ASSERT_NE(controller.Draft_annotation(), nullptr);

    EXPECT_TRUE(controller.On_cancel());
    EXPECT_EQ(controller.Draft_annotation(), nullptr);
    EXPECT_TRUE(controller.Annotations().empty());
}

TEST(annotation_controller,
     AnnotationEditTargetAt_PrefersSelectedLineHandlesAndFallsBackToBody) {
    AnnotationController controller;
    controller.Insert_annotation_at(0, Make_line(1, {40, 40}, {80, 50}, 6),
                                    std::optional<uint64_t>{1});

    EXPECT_EQ(controller.Annotation_edit_target_at({40, 40}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{1, AnnotationEditTargetKind::LineStartHandle}}));
    EXPECT_EQ(controller.Annotation_edit_target_at({80, 50}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{1, AnnotationEditTargetKind::LineEndHandle}}));
    EXPECT_EQ(controller.Annotation_edit_target_at({60, 45}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{1, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_controller,
     AnnotationEditTargetAt_PrefersSelectedRectangleHandlesAndFallsBackToBody) {
    AnnotationController controller;
    controller.Insert_annotation_at(
        0, Make_rectangle(1, RectPx::From_ltrb(40, 40, 81, 81), 4),
        std::optional<uint64_t>{1});

    EXPECT_EQ(controller.Annotation_edit_target_at({40, 40}),
              (std::optional<AnnotationEditTarget>{AnnotationEditTarget{
                  1, AnnotationEditTargetKind::RectangleTopLeftHandle}}));
    EXPECT_EQ(controller.Annotation_edit_target_at({60, 40}),
              (std::optional<AnnotationEditTarget>{AnnotationEditTarget{
                  1, AnnotationEditTargetKind::RectangleTopHandle}}));
    EXPECT_EQ(controller.Annotation_edit_target_at({50, 42}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{1, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_controller,
     AnnotationEditTargetAt_PrefersSelectedEllipseHandlesAndFallsBackToBody) {
    AnnotationController controller;
    controller.Insert_annotation_at(
        0, Make_ellipse(1, RectPx::From_ltrb(40, 40, 81, 81), 4, true),
        std::optional<uint64_t>{1});

    EXPECT_EQ(controller.Annotation_edit_target_at({40, 40}),
              (std::optional<AnnotationEditTarget>{AnnotationEditTarget{
                  1, AnnotationEditTargetKind::RectangleTopLeftHandle}}));
    EXPECT_EQ(controller.Annotation_edit_target_at({60, 60}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{1, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_controller, AnnotationDragRelease_MovesAnnotationAndIsUndoable) {
    AnnotationController controller;
    UndoStack undo_stack;
    Annotation const original = Make_stroke(1, {{40, 40}, {60, 40}});
    RectPx const orig_bounds = Annotation_visual_bounds(original);
    RectPx const expected_bounds =
        RectPx::From_ltrb(orig_bounds.left + 20, orig_bounds.top + 20,
                          orig_bounds.right + 20, orig_bounds.bottom + 20);

    controller.Insert_annotation_at(0, original, std::nullopt);

    ASSERT_TRUE(controller.Begin_annotation_edit(
        AnnotationEditTarget{1, AnnotationEditTargetKind::Body}, {50, 40}));
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});
    EXPECT_TRUE(controller.On_pointer_move({70, 60}));
    EXPECT_EQ(
        std::get<FreehandStrokeAnnotation>(controller.Annotations()[0].data).points[0],
        (PointPx{60, 60}));
    EXPECT_EQ(controller.Selected_annotation_bounds(),
              std::optional<RectPx>{expected_bounds});

    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_FALSE(controller.Is_annotation_dragging());

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0], original);
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});

    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(
        std::get<FreehandStrokeAnnotation>(controller.Annotations()[0].data).points[0],
        (PointPx{60, 60}));
    EXPECT_EQ(controller.Selected_annotation_bounds(),
              std::optional<RectPx>{expected_bounds});
}

TEST(annotation_controller, CancelDuringAnnotationDrag_RestoresOriginalAnnotation) {
    AnnotationController controller;
    Annotation const original = Make_stroke(1, {{40, 40}, {60, 40}});
    controller.Insert_annotation_at(0, original, std::nullopt);

    ASSERT_TRUE(controller.Begin_annotation_edit(
        AnnotationEditTarget{1, AnnotationEditTargetKind::Body}, {50, 40}));
    EXPECT_TRUE(controller.On_pointer_move({80, 70}));
    ASSERT_NE(controller.Annotations()[0], original);

    EXPECT_TRUE(controller.On_cancel());
    EXPECT_FALSE(controller.Is_annotation_dragging());
    EXPECT_EQ(controller.Annotations()[0], original);
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});
}

TEST(annotation_controller, ArrowBodyDragRelease_PreservesArrowHeadCoverage) {
    AnnotationController controller;
    UndoStack undo_stack;
    Annotation const original = Make_line(1, {10, 10}, {50, 10}, 4, true);
    controller.Insert_annotation_at(0, original, std::optional<uint64_t>{1});

    ASSERT_TRUE(controller.Begin_annotation_edit(
        AnnotationEditTarget{1, AnnotationEditTargetKind::Body}, {30, 10}));
    EXPECT_TRUE(controller.On_pointer_move({50, 25}));
    {
        auto const &moved_arrow =
            std::get<LineAnnotation>(controller.Annotations()[0].data);
        EXPECT_TRUE(moved_arrow.arrow_head);
        EXPECT_EQ(moved_arrow.start, (PointPx{30, 25}));
        EXPECT_EQ(moved_arrow.end, (PointPx{70, 25}));
    }
    EXPECT_TRUE(Annotation_hits_point(controller.Annotations()[0], {70, 25}));
    EXPECT_TRUE(Annotation_hits_point(controller.Annotations()[0], {60, 22}));

    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0], original);

    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_TRUE(std::get<LineAnnotation>(controller.Annotations()[0].data).arrow_head);
    EXPECT_TRUE(Annotation_hits_point(controller.Annotations()[0], {70, 25}));
    EXPECT_TRUE(Annotation_hits_point(controller.Annotations()[0], {60, 22}));
}

TEST(annotation_controller, LineEndpointDragRelease_UpdatesLineAndIsUndoable) {
    AnnotationController controller;
    UndoStack undo_stack;
    Annotation const original = Make_line(1, {40, 40}, {90, 40}, 6);
    controller.Insert_annotation_at(0, original, std::optional<uint64_t>{1});

    ASSERT_TRUE(controller.Begin_annotation_edit(
        AnnotationEditTarget{1, AnnotationEditTargetKind::LineStartHandle}, {40, 40}));
    EXPECT_EQ(
        controller.Active_annotation_edit_handle(),
        std::optional<AnnotationEditHandleKind>{AnnotationEditHandleKind::LineStart});
    EXPECT_TRUE(controller.On_pointer_move({60, 65}));
    {
        auto const &edited_line =
            std::get<LineAnnotation>(controller.Annotations()[0].data);
        EXPECT_EQ(edited_line.start, (PointPx{60, 65}));
        EXPECT_EQ(edited_line.end, (PointPx{90, 40}));
    }

    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Active_annotation_edit_handle(), std::nullopt);

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0], original);
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});

    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    {
        auto const &redone_line =
            std::get<LineAnnotation>(controller.Annotations()[0].data);
        EXPECT_EQ(redone_line.start, (PointPx{60, 65}));
        EXPECT_EQ(redone_line.end, (PointPx{90, 40}));
    }
}

TEST(annotation_controller, CancelDuringLineEndpointDrag_RestoresOriginalLine) {
    AnnotationController controller;
    Annotation const original = Make_line(1, {40, 40}, {90, 40}, 6);
    controller.Insert_annotation_at(0, original, std::optional<uint64_t>{1});

    ASSERT_TRUE(controller.Begin_annotation_edit(
        AnnotationEditTarget{1, AnnotationEditTargetKind::LineEndHandle}, {90, 40}));
    EXPECT_TRUE(controller.On_pointer_move({120, 70}));
    ASSERT_NE(controller.Annotations()[0], original);

    EXPECT_TRUE(controller.On_cancel());
    EXPECT_EQ(controller.Active_annotation_edit_handle(), std::nullopt);
    EXPECT_EQ(controller.Annotations()[0], original);
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});
}

TEST(annotation_controller, ArrowEndpointDragRelease_UpdatesArrowAndPreservesHead) {
    AnnotationController controller;
    UndoStack undo_stack;
    Annotation const original = Make_line(1, {40, 40}, {90, 40}, 6, true);
    controller.Insert_annotation_at(0, original, std::optional<uint64_t>{1});

    ASSERT_TRUE(controller.Begin_annotation_edit(
        AnnotationEditTarget{1, AnnotationEditTargetKind::LineEndHandle}, {90, 40}));
    EXPECT_EQ(
        controller.Active_annotation_edit_handle(),
        std::optional<AnnotationEditHandleKind>{AnnotationEditHandleKind::LineEnd});
    EXPECT_TRUE(controller.On_pointer_move({120, 70}));
    {
        auto const &edited_arrow =
            std::get<LineAnnotation>(controller.Annotations()[0].data);
        EXPECT_TRUE(edited_arrow.arrow_head);
        EXPECT_EQ(edited_arrow.start, (PointPx{40, 40}));
        EXPECT_EQ(edited_arrow.end, (PointPx{120, 70}));
    }

    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Active_annotation_edit_handle(), std::nullopt);

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0], original);

    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    {
        auto const &redone_arrow =
            std::get<LineAnnotation>(controller.Annotations()[0].data);
        EXPECT_TRUE(redone_arrow.arrow_head);
        EXPECT_EQ(redone_arrow.end, (PointPx{120, 70}));
    }
}

TEST(annotation_controller, RectangleHandleDragRelease_UpdatesBoundsAndIsUndoable) {
    AnnotationController controller;
    UndoStack undo_stack;
    Annotation const original = Make_rectangle(1, RectPx::From_ltrb(40, 40, 81, 81), 6);
    controller.Insert_annotation_at(0, original, std::optional<uint64_t>{1});

    ASSERT_TRUE(controller.Begin_annotation_edit(
        AnnotationEditTarget{1, AnnotationEditTargetKind::RectangleRightHandle},
        {80, 60}));
    EXPECT_EQ(controller.Active_annotation_edit_handle(),
              std::optional<AnnotationEditHandleKind>{
                  AnnotationEditHandleKind::RectangleRight});
    EXPECT_TRUE(controller.On_pointer_move({100, 60}));
    EXPECT_EQ(
        std::get<RectangleAnnotation>(controller.Annotations()[0].data).outer_bounds,
        (RectPx::From_ltrb(40, 40, 101, 81)));

    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Active_annotation_edit_handle(), std::nullopt);

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0], original);
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});

    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(
        std::get<RectangleAnnotation>(controller.Annotations()[0].data).outer_bounds,
        (RectPx::From_ltrb(40, 40, 101, 81)));
}

TEST(annotation_controller, EllipseHandleDragRelease_UpdatesBoundsAndIsUndoable) {
    AnnotationController controller;
    UndoStack undo_stack;
    Annotation const original = Make_ellipse(1, RectPx::From_ltrb(40, 40, 81, 81), 6);
    controller.Insert_annotation_at(0, original, std::optional<uint64_t>{1});

    ASSERT_TRUE(controller.Begin_annotation_edit(
        AnnotationEditTarget{1, AnnotationEditTargetKind::RectangleRightHandle},
        {80, 60}));
    EXPECT_EQ(controller.Active_annotation_edit_handle(),
              std::optional<AnnotationEditHandleKind>{
                  AnnotationEditHandleKind::RectangleRight});
    EXPECT_TRUE(controller.On_pointer_move({100, 60}));
    EXPECT_EQ(
        std::get<EllipseAnnotation>(controller.Annotations()[0].data).outer_bounds,
        (RectPx::From_ltrb(40, 40, 101, 81)));

    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Active_annotation_edit_handle(), std::nullopt);

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Annotations()[0], original);

    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(
        std::get<EllipseAnnotation>(controller.Annotations()[0].data).outer_bounds,
        (RectPx::From_ltrb(40, 40, 101, 81)));
}

TEST(annotation_controller, DeleteSelectedAnnotation_IsUndoableAndRedoable) {
    AnnotationController controller;
    UndoStack undo_stack;

    controller.Insert_annotation_at(0, Make_stroke(1, {{20, 20}, {30, 20}}),
                                    std::nullopt);
    EXPECT_TRUE(controller.Set_selected_annotation(std::optional<uint64_t>{1}));
    ASSERT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});

    EXPECT_TRUE(controller.Delete_selected_annotation(undo_stack));
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);

    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Selected_annotation_id(), std::optional<uint64_t>{1});

    undo_stack.Redo();
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Selected_annotation_id(), std::nullopt);
}

TEST(annotation_controller, SelectedAnnotationBounds_FreehandUsesVisualBounds) {
    AnnotationController controller;
    Annotation const stroke = Make_stroke(1, {{40, 40}, {60, 40}});
    controller.Insert_annotation_at(0, stroke, std::nullopt);
    ASSERT_TRUE(controller.Set_selected_annotation(std::optional<uint64_t>{1}));

    EXPECT_EQ(controller.Selected_annotation_bounds(),
              std::optional<RectPx>{Annotation_visual_bounds(stroke)});
}

TEST(annotation_controller, SelectedAnnotationBounds_LineUsesVisualNotHitTestBounds) {
    AnnotationController controller;
    Annotation const line = Make_line(1, {10, 100}, {200, 100}, 2);
    controller.Insert_annotation_at(0, line, std::optional<uint64_t>{1});

    std::optional<RectPx> const bounds = controller.Selected_annotation_bounds();

    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(*bounds, Annotation_visual_bounds(line));
    EXPECT_NE(*bounds, Annotation_bounds(line));
}

// ---------------------------------------------------------------------------
// Bubble tool — hotkey, font, counter
// ---------------------------------------------------------------------------

TEST(annotation_controller, ToggleToolByHotkey_ActivatesAndDeactivatesBubble) {
    AnnotationController controller;

    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'N'));
    EXPECT_EQ(controller.Active_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Bubble});
    EXPECT_TRUE(controller.Toggle_tool_by_hotkey(L'n'));
    EXPECT_EQ(controller.Active_tool(), std::nullopt);
}

TEST(annotation_controller, BubbleFontChoice_DefaultsToSans) {
    AnnotationController controller;
    EXPECT_EQ(controller.Bubble_current_font(), TextFontChoice::Sans);
}

TEST(annotation_controller, BubbleFontChoice_PersistsAfterSet) {
    AnnotationController controller;

    controller.Set_bubble_current_font(TextFontChoice::Serif);
    EXPECT_EQ(controller.Bubble_current_font(), TextFontChoice::Serif);

    controller.Set_bubble_current_font(TextFontChoice::Mono);
    EXPECT_EQ(controller.Bubble_current_font(), TextFontChoice::Mono);
}

TEST(annotation_controller, BubbleCounter_InitialValueIsOne) {
    AnnotationController controller;
    EXPECT_EQ(controller.Current_bubble_counter(), 1);
}

TEST(annotation_controller, BubblePlacement_ShowsSingleDraftOnPressAndMove) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble));

    EXPECT_TRUE(controller.On_primary_press({50, 50}));
    EXPECT_TRUE(controller.Has_active_gesture());
    EXPECT_TRUE(controller.Annotations().empty());
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(controller.Draft_annotation()->Kind(), AnnotationKind::Bubble);
    EXPECT_EQ(std::get<BubbleAnnotation>(controller.Draft_annotation()->data).center,
              (PointPx{50, 50}));

    EXPECT_TRUE(controller.On_pointer_move({80, 90}));
    EXPECT_TRUE(controller.Annotations().empty());
    ASSERT_NE(controller.Draft_annotation(), nullptr);
    EXPECT_EQ(std::get<BubbleAnnotation>(controller.Draft_annotation()->data).center,
              (PointPx{80, 90}));
}

TEST(annotation_controller, BubblePlacement_PlacesWithCurrentCounterThenIncrements) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    UndoStack undo_stack;
    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble));

    EXPECT_EQ(controller.Current_bubble_counter(), 1);
    EXPECT_TRUE(controller.On_primary_press({50, 50}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(
        std::get<BubbleAnnotation>(controller.Annotations()[0].data).counter_value, 1);
    EXPECT_EQ(controller.Current_bubble_counter(), 2);
}

TEST(annotation_controller, BubblePlacement_UndoDecrementsCounter) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    UndoStack undo_stack;
    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble));

    EXPECT_TRUE(controller.On_primary_press({50, 50}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Current_bubble_counter(), 2);

    undo_stack.Undo();
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Current_bubble_counter(), 1);
}

TEST(annotation_controller, BubblePlacement_RedoIncrementsCounter) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    UndoStack undo_stack;
    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble));

    EXPECT_TRUE(controller.On_primary_press({50, 50}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    undo_stack.Undo();
    EXPECT_EQ(controller.Current_bubble_counter(), 1);

    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Current_bubble_counter(), 2);
}

TEST(annotation_controller, BubblePlacement_DeleteDoesNotAffectCounter) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    UndoStack undo_stack;
    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble));

    EXPECT_TRUE(controller.On_primary_press({50, 50}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Current_bubble_counter(), 2);

    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble)); // deactivate
    uint64_t const id = controller.Annotations()[0].id;
    EXPECT_TRUE(controller.Set_selected_annotation(std::optional<uint64_t>{id}));
    EXPECT_TRUE(controller.Delete_selected_annotation(undo_stack));
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Current_bubble_counter(), 2);
}

TEST(annotation_controller, BubblePlacement_UndoDeleteDoesNotAffectCounter) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    UndoStack undo_stack;
    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble));

    EXPECT_TRUE(controller.On_primary_press({50, 50}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));

    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble)); // deactivate
    uint64_t const id = controller.Annotations()[0].id;
    EXPECT_TRUE(controller.Set_selected_annotation(std::optional<uint64_t>{id}));
    EXPECT_TRUE(controller.Delete_selected_annotation(undo_stack));
    EXPECT_EQ(controller.Current_bubble_counter(), 2);

    undo_stack.Undo(); // undo Delete
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Current_bubble_counter(), 2);
}

TEST(annotation_controller, BubblePlacement_SwitchingToolsDoesNotResetCounter) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    UndoStack undo_stack;
    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble));

    EXPECT_TRUE(controller.On_primary_press({50, 50}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Current_bubble_counter(), 2);

    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble));   // off
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Freehand)); // on
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Freehand)); // off
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble));   // back on
    EXPECT_EQ(controller.Current_bubble_counter(), 2);
}

TEST(annotation_controller, BubblePlacement_FullCounterSequence) {
    AnnotationController controller;
    FakeTextLayoutEngine engine;
    UndoStack undo_stack;
    controller.Set_text_layout_engine(&engine);
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble));

    // Add bubble: counter 1→2, shows "1"
    EXPECT_TRUE(controller.On_primary_press({10, 10}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Current_bubble_counter(), 2);
    EXPECT_EQ(
        std::get<BubbleAnnotation>(controller.Annotations()[0].data).counter_value, 1);

    // Add bubble: counter 2→3, shows "2"
    EXPECT_TRUE(controller.On_primary_press({20, 20}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Current_bubble_counter(), 3);
    EXPECT_EQ(
        std::get<BubbleAnnotation>(controller.Annotations()[1].data).counter_value, 2);

    // Undo(Add): counter 3→2, "2" removed
    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Current_bubble_counter(), 2);

    // Delete "1": counter unchanged at 2
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble)); // deactivate
    uint64_t const id_1 = controller.Annotations()[0].id;
    EXPECT_TRUE(controller.Set_selected_annotation(std::optional<uint64_t>{id_1}));
    EXPECT_TRUE(controller.Delete_selected_annotation(undo_stack));
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Current_bubble_counter(), 2);

    // Add bubble: counter 2→3, shows "2"
    EXPECT_TRUE(controller.Toggle_tool(AnnotationToolId::Bubble)); // reactivate
    EXPECT_TRUE(controller.On_primary_press({30, 30}));
    EXPECT_TRUE(controller.On_primary_release(undo_stack));
    EXPECT_EQ(controller.Current_bubble_counter(), 3);
    EXPECT_EQ(
        std::get<BubbleAnnotation>(controller.Annotations().back().data).counter_value,
        2);

    // Undo(Add): counter 3→2
    undo_stack.Undo();
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Current_bubble_counter(), 2);

    // Undo(Delete): "1" restored, counter unchanged at 2
    undo_stack.Undo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Current_bubble_counter(), 2);

    // Redo(Delete): "1" removed, counter unchanged at 2
    undo_stack.Redo();
    EXPECT_TRUE(controller.Annotations().empty());
    EXPECT_EQ(controller.Current_bubble_counter(), 2);

    // Redo(Add): "2" restored, counter 2→3
    undo_stack.Redo();
    ASSERT_EQ(controller.Annotations().size(), 1u);
    EXPECT_EQ(controller.Current_bubble_counter(), 3);
}
