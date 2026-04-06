#include "fake_text_layout_engine.h"
#include "greenflame_core/annotation_commands.h"
#include "greenflame_core/annotation_controller.h"
#include "greenflame_core/text_edit_controller.h"
#include "greenflame_core/undo_stack.h"

using namespace greenflame::core;

namespace {

// FakeTextLayoutEngine: char_width_px=10, line_height_px=20.

TextAnnotationBaseStyle Default_base_style() {
    return {
        .color = RGB(0xFF, 0x00, 0x00),
        .font_choice = TextFontChoice::Sans,
        .point_size = 12,
    };
}

// Build a committed TextAnnotation with a non-empty premultiplied_bgra so it
// passes Text_annotation_has_text and looks like a rasterized annotation.
Annotation Make_committed_text(uint64_t id, PointPx origin, RectPx visual_bounds,
                               std::wstring text = L"hello") {
    Annotation ann{};
    ann.id = id;

    TextAnnotation ta{};
    ta.origin = origin;
    ta.base_style = Default_base_style();
    ta.runs = {TextRun{std::move(text), {}}};
    ta.visual_bounds = visual_bounds;
    ta.bitmap_width_px = visual_bounds.Width();
    ta.bitmap_height_px = visual_bounds.Height();
    ta.bitmap_row_bytes = ta.bitmap_width_px * 4;
    ta.premultiplied_bgra.assign(static_cast<size_t>(ta.bitmap_row_bytes) *
                                     static_cast<size_t>(ta.bitmap_height_px),
                                 0xFF);
    ann.data = std::move(ta);
    return ann;
}

struct TextReditFixture {
    TextReditFixture(TextReditFixture const &) = delete;
    TextReditFixture &operator=(TextReditFixture const &) = delete;

    FakeTextLayoutEngine engine{};
    AnnotationController ctrl{};
    UndoStack undo_stack{};

    static constexpr uint64_t kAnnotationId = 42;
    static constexpr PointPx kOrigin = {0, 0};
    // "hello" = 5 chars * 10px = 50px wide, 1 line * 20px tall
    static constexpr RectPx kBounds = RectPx::From_ltrb(0, 0, 50, 20);

    TextReditFixture() {
        ctrl.Set_text_layout_engine(&engine);
        ctrl.Insert_annotation_at(0,
                                  Make_committed_text(kAnnotationId, kOrigin, kBounds),
                                  std::optional<uint64_t>{kAnnotationId});
    }

    [[nodiscard]] TextAnnotation const *Current_text_annotation() const {
        for (Annotation const &ann : ctrl.Annotations()) {
            if (ann.id == kAnnotationId) {
                return std::get_if<TextAnnotation>(&ann.data);
            }
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// TextEditController: typing style syncs to cursor position
//
// Text layout: char_width_px=10, origin=(0,0)
// Runs used below: "aaa" (normal, offsets 0-2) + "BBB" (bold, offsets 3-5)
//                  + "aaa" (normal, offsets 6-8)
// Pixel x for column N = N * 10.
// ---------------------------------------------------------------------------

// Helper: find the first run containing the given character and return its
// bold flag.  Fails the test (via EXPECT) if the character is not found.
[[nodiscard]] static bool Find_char_bold(std::vector<TextRun> const &runs, wchar_t ch) {
    for (TextRun const &run : runs) {
        if (run.text.find(ch) != std::wstring::npos) {
            return run.flags.bold;
        }
    }
    ADD_FAILURE() << "character '" << static_cast<char>(ch) << "' not found in runs";
    return false;
}

static std::vector<TextRun> Mixed_bold_runs() {
    return {TextRun{L"aaa", {}}, TextRun{L"BBB", {.bold = true}}, TextRun{L"aaa", {}}};
}

TEST(TextEditControllerTypingStyle, PointerPress_InBoldRun_TypingIsBold) {
    FakeTextLayoutEngine engine{};
    TextEditController ctrl({0, 0}, Default_base_style(), Mixed_bold_runs(), &engine);

    // x=40 → column 4 → offset 4; left char is B[3] (bold)
    ctrl.On_pointer_press({40, 0});
    ctrl.On_text_input(L"x");

    TextDraftView const view = ctrl.Build_view();
    ASSERT_NE(view.annotation, nullptr);
    EXPECT_TRUE(Find_char_bold(view.annotation->runs, L'x'));
}

TEST(TextEditControllerTypingStyle, PointerPress_InNormalRun_TypingIsNotBold) {
    FakeTextLayoutEngine engine{};
    TextEditController ctrl({0, 0}, Default_base_style(), Mixed_bold_runs(), &engine);

    // First, position cursor into bold so typing_style would be bold...
    ctrl.On_pointer_press({40, 0});
    // ...then click back into normal region: x=10 → offset 1, left char a[0] (not bold)
    ctrl.On_pointer_press({10, 0});
    ctrl.On_text_input(L"x");

    TextDraftView const view = ctrl.Build_view();
    ASSERT_NE(view.annotation, nullptr);
    EXPECT_FALSE(Find_char_bold(view.annotation->runs, L'x'));
}

TEST(TextEditControllerTypingStyle, Navigation_RightIntoBoldRun_TypingIsBold) {
    FakeTextLayoutEngine engine{};
    TextEditController ctrl({0, 0}, Default_base_style(), Mixed_bold_runs(), &engine);

    // x=30 → offset 3; left char is a[2] (not bold)
    ctrl.On_pointer_press({30, 0});
    // Arrow right → offset 4; left char is B[3] (bold)
    ctrl.On_navigation(TextNavigationAction::Right, false);
    ctrl.On_text_input(L"x");

    TextDraftView const view = ctrl.Build_view();
    ASSERT_NE(view.annotation, nullptr);
    EXPECT_TRUE(Find_char_bold(view.annotation->runs, L'x'));
}

TEST(TextEditControllerTypingStyle, Backspace_MovesOutOfBoldRun_TypingBecomesNotBold) {
    FakeTextLayoutEngine engine{};
    TextEditController ctrl({0, 0}, Default_base_style(), Mixed_bold_runs(), &engine);

    // x=40 → offset 4; left char is B[3] (bold)
    ctrl.On_pointer_press({40, 0});
    // Backspace deletes B[3]; cursor moves to offset 3; left char is a[2] (not bold)
    ctrl.On_backspace(false);
    ctrl.On_text_input(L"x");

    TextDraftView const view = ctrl.Build_view();
    ASSERT_NE(view.annotation, nullptr);
    EXPECT_FALSE(Find_char_bold(view.annotation->runs, L'x'));
}

// ---------------------------------------------------------------------------
// TextEditController: constructor with initial runs
// ---------------------------------------------------------------------------

TEST(TextEditControllerRedit, InitialRunsArePreloaded) {
    FakeTextLayoutEngine engine{};
    std::vector<TextRun> runs = {TextRun{L"hello", {.bold = true}},
                                 TextRun{L" world", {}}};
    TextEditController ctrl({0, 0}, Default_base_style(), runs, &engine);

    TextDraftView const view = ctrl.Build_view();
    ASSERT_NE(view.annotation, nullptr);
    ASSERT_EQ(view.annotation->runs.size(), 2u);
    EXPECT_EQ(view.annotation->runs[0].text, L"hello");
    EXPECT_TRUE(view.annotation->runs[0].flags.bold);
    EXPECT_EQ(view.annotation->runs[1].text, L" world");
}

TEST(TextEditControllerRedit, EmptyInitialRunsProducesEmptyDraft) {
    FakeTextLayoutEngine engine{};
    TextEditController ctrl({0, 0}, Default_base_style(), {}, &engine);

    TextDraftView const view = ctrl.Build_view();
    ASSERT_NE(view.annotation, nullptr);
    EXPECT_TRUE(view.annotation->runs.empty() ||
                Flatten_text(view.annotation->runs).empty());
}

TEST(TextEditControllerRedit, UndoAfterTypingRestoresInitialRuns) {
    FakeTextLayoutEngine engine{};
    TextEditController ctrl({0, 0}, Default_base_style(), {TextRun{L"abc", {}}},
                            &engine);

    ctrl.On_text_input(L"xyz");
    ctrl.Undo(); // undo typing — should return to "abc"

    TextDraftView const view = ctrl.Build_view();
    ASSERT_NE(view.annotation, nullptr);
    EXPECT_EQ(Flatten_text(view.annotation->runs), L"abc");
}

// ---------------------------------------------------------------------------
// AnnotationController::Begin_text_edit_on_annotation
// ---------------------------------------------------------------------------

TEST(AnnotationControllerRedit, BeginTextEditOnAnnotation_SetsActiveEdit) {
    TextReditFixture f;
    PointPx const click{0, 0};

    bool const ok =
        f.ctrl.Begin_text_edit_on_annotation(TextReditFixture::kAnnotationId, click);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(f.ctrl.Has_active_text_edit());
    ASSERT_EQ(f.ctrl.Editing_annotation_id(), TextReditFixture::kAnnotationId);
}

TEST(AnnotationControllerRedit, BeginTextEditOnAnnotation_PreservesOriginalRuns) {
    TextReditFixture f;
    ASSERT_TRUE(
        f.ctrl.Begin_text_edit_on_annotation(TextReditFixture::kAnnotationId, {0, 0}));

    TextEditController const *const edit = f.ctrl.Active_text_edit();
    ASSERT_NE(edit, nullptr);
    TextDraftView const view = edit->Build_view();
    ASSERT_NE(view.annotation, nullptr);
    EXPECT_EQ(Flatten_text(view.annotation->runs), L"hello");
}

TEST(AnnotationControllerRedit, BeginTextEditOnAnnotation_UnknownId_ReturnsFalse) {
    TextReditFixture f;
    constexpr uint64_t bad_id = 999;

    bool const ok = f.ctrl.Begin_text_edit_on_annotation(bad_id, {0, 0});

    EXPECT_FALSE(ok);
    EXPECT_FALSE(f.ctrl.Has_active_text_edit());
    EXPECT_FALSE(f.ctrl.Editing_annotation_id().has_value());
}

TEST(AnnotationControllerRedit,
     BeginTextEditOnAnnotation_NonTextAnnotation_ReturnsFalse) {
    FakeTextLayoutEngine engine{};
    AnnotationController ctrl{};
    ctrl.Set_text_layout_engine(&engine);

    Annotation line_ann{};
    line_ann.id = 7;
    line_ann.data = LineAnnotation{.start = {0, 0}, .end = {10, 10}};
    ctrl.Insert_annotation_at(0, line_ann, std::optional<uint64_t>{7u});

    bool const ok = ctrl.Begin_text_edit_on_annotation(7, {0, 0});

    EXPECT_FALSE(ok);
    EXPECT_FALSE(ctrl.Has_active_text_edit());
}

TEST(AnnotationControllerRedit, BeginTextEditOnAnnotation_NoLayoutEngine_ReturnsFalse) {
    AnnotationController ctrl{}; // no layout engine set
    Annotation ann = Make_committed_text(1, {0, 0}, RectPx::From_ltrb(0, 0, 50, 20));
    ctrl.Insert_annotation_at(0, ann, std::optional<uint64_t>{1u});

    bool const ok = ctrl.Begin_text_edit_on_annotation(1, {0, 0});

    EXPECT_FALSE(ok);
    EXPECT_FALSE(ctrl.Has_active_text_edit());
}

// ---------------------------------------------------------------------------
// AnnotationController::Commit_text_edit_annotation
// ---------------------------------------------------------------------------

TEST(AnnotationControllerRedit, CommitTextEditAnnotation_UpdatesAnnotation) {
    TextReditFixture f;
    // Click at (50, 0): past end of "hello" (5 chars * 10px), FakeTextLayoutEngine
    // clamps cursor to offset 5 (end of text).
    ASSERT_TRUE(
        f.ctrl.Begin_text_edit_on_annotation(TextReditFixture::kAnnotationId, {50, 0}));

    TextEditController *const edit = f.ctrl.Active_text_edit();
    ASSERT_NE(edit, nullptr);
    edit->On_text_input(L" world"); // append to "hello"

    TextAnnotation committed = edit->Commit();
    f.ctrl.Commit_text_edit_annotation(f.undo_stack, std::move(committed));

    EXPECT_FALSE(f.ctrl.Has_active_text_edit());
    EXPECT_FALSE(f.ctrl.Editing_annotation_id().has_value());

    TextAnnotation const *const updated = f.Current_text_annotation();
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(Flatten_text(updated->runs), L"hello world");
}

TEST(AnnotationControllerRedit, CommitTextEditAnnotation_PushesUndoEntry) {
    TextReditFixture f;
    ASSERT_TRUE(
        f.ctrl.Begin_text_edit_on_annotation(TextReditFixture::kAnnotationId, {50, 0}));
    TextEditController *const edit = f.ctrl.Active_text_edit();
    ASSERT_NE(edit, nullptr);
    edit->On_text_input(L" world");
    f.ctrl.Commit_text_edit_annotation(f.undo_stack, edit->Commit());

    EXPECT_TRUE(f.undo_stack.Can_undo());
}

TEST(AnnotationControllerRedit, CommitTextEditAnnotation_UndoRestoresOriginal) {
    TextReditFixture f;
    ASSERT_TRUE(
        f.ctrl.Begin_text_edit_on_annotation(TextReditFixture::kAnnotationId, {50, 0}));
    TextEditController *const edit = f.ctrl.Active_text_edit();
    ASSERT_NE(edit, nullptr);
    edit->On_text_input(L" world");
    f.ctrl.Commit_text_edit_annotation(f.undo_stack, edit->Commit());

    f.undo_stack.Undo();

    TextAnnotation const *const restored = f.Current_text_annotation();
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(Flatten_text(restored->runs), L"hello");
}

TEST(AnnotationControllerRedit, CommitTextEditAnnotation_EmptyText_KeepsOriginal) {
    TextReditFixture f;
    ASSERT_TRUE(
        f.ctrl.Begin_text_edit_on_annotation(TextReditFixture::kAnnotationId, {0, 0}));
    TextEditController *const edit = f.ctrl.Active_text_edit();
    ASSERT_NE(edit, nullptr);
    edit->On_select_all();
    edit->On_delete(false); // delete all text

    TextAnnotation committed = edit->Commit();
    f.ctrl.Commit_text_edit_annotation(f.undo_stack, std::move(committed));

    EXPECT_FALSE(f.ctrl.Has_active_text_edit());
    EXPECT_FALSE(f.undo_stack.Can_undo()); // no undo entry added

    TextAnnotation const *const ann = f.Current_text_annotation();
    ASSERT_NE(ann, nullptr);
    EXPECT_EQ(Flatten_text(ann->runs), L"hello"); // original preserved
}

// ---------------------------------------------------------------------------
// Cancel during re-edit
// ---------------------------------------------------------------------------

TEST(AnnotationControllerRedit, CancelRedit_LeavesOriginalAnnotationUnchanged) {
    TextReditFixture f;
    ASSERT_TRUE(
        f.ctrl.Begin_text_edit_on_annotation(TextReditFixture::kAnnotationId, {0, 0}));
    TextEditController *const edit = f.ctrl.Active_text_edit();
    ASSERT_NE(edit, nullptr);
    edit->On_text_input(L" extra");

    ASSERT_TRUE(f.ctrl.On_cancel());

    EXPECT_FALSE(f.ctrl.Has_active_text_edit());
    EXPECT_FALSE(f.ctrl.Editing_annotation_id().has_value());

    TextAnnotation const *const ann = f.Current_text_annotation();
    ASSERT_NE(ann, nullptr);
    EXPECT_EQ(Flatten_text(ann->runs), L"hello");
}

TEST(AnnotationControllerRedit, CancelTextDraft_ClearsEditingAnnotationId) {
    TextReditFixture f;
    ASSERT_TRUE(
        f.ctrl.Begin_text_edit_on_annotation(TextReditFixture::kAnnotationId, {0, 0}));

    f.ctrl.Cancel_text_draft();

    EXPECT_FALSE(f.ctrl.Has_active_text_edit());
    EXPECT_FALSE(f.ctrl.Editing_annotation_id().has_value());
}

// ---------------------------------------------------------------------------
// Editing_annotation_id is cleared on Reset_for_session / Clear_annotations
// ---------------------------------------------------------------------------

TEST(AnnotationControllerRedit, ResetForSession_ClearsEditingAnnotationId) {
    TextReditFixture f;
    ASSERT_TRUE(
        f.ctrl.Begin_text_edit_on_annotation(TextReditFixture::kAnnotationId, {0, 0}));
    ASSERT_TRUE(f.ctrl.Editing_annotation_id().has_value());

    f.ctrl.Reset_for_session();

    EXPECT_FALSE(f.ctrl.Editing_annotation_id().has_value());
}

TEST(AnnotationControllerRedit, ClearAnnotations_ClearsEditingAnnotationId) {
    TextReditFixture f;
    ASSERT_TRUE(
        f.ctrl.Begin_text_edit_on_annotation(TextReditFixture::kAnnotationId, {0, 0}));
    ASSERT_TRUE(f.ctrl.Editing_annotation_id().has_value());

    f.ctrl.Clear_annotations();

    EXPECT_FALSE(f.ctrl.Editing_annotation_id().has_value());
}

} // namespace
