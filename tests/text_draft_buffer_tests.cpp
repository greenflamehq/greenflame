#include "greenflame_core/text_edit_controller.h"

using namespace greenflame::core;

namespace {

constexpr int32_t kCharWidthPx = 10;
constexpr int32_t kLineHeightPx = 20;

[[nodiscard]] std::wstring Flatten_text(std::span<const TextRun> runs) {
    std::wstring text;
    for (TextRun const &run : runs) {
        text += run.text;
    }
    return text;
}

[[nodiscard]] std::vector<int32_t> Line_starts(std::wstring_view text) {
    std::vector<int32_t> starts = {0};
    for (size_t index = 0; index < text.size(); ++index) {
        if (text[index] == L'\n' && index + 1 < text.size()) {
            starts.push_back(static_cast<int32_t>(index + 1));
        }
    }
    return starts;
}

[[nodiscard]] int32_t Line_index_for_offset(std::wstring_view text, int32_t offset) {
    std::vector<int32_t> const starts = Line_starts(text);
    int32_t line_index = 0;
    for (size_t index = 1; index < starts.size(); ++index) {
        if (offset < starts[index]) {
            break;
        }
        line_index = static_cast<int32_t>(index);
    }
    return line_index;
}

[[nodiscard]] int32_t Line_end(std::wstring_view text, int32_t line_start) {
    int32_t line_end = line_start;
    while (line_end < static_cast<int32_t>(text.size()) &&
           text[static_cast<size_t>(line_end)] != L'\n') {
        ++line_end;
    }
    return line_end;
}

[[nodiscard]] int32_t Column_for_offset(std::wstring_view text, int32_t offset) {
    int32_t const clamped_offset =
        std::clamp(offset, 0, static_cast<int32_t>(text.size()));
    int32_t const line_index = Line_index_for_offset(text, clamped_offset);
    std::vector<int32_t> const starts = Line_starts(text);
    return clamped_offset - starts[static_cast<size_t>(line_index)];
}

[[nodiscard]] int32_t Offset_for_line_and_column(std::wstring_view text, int32_t line,
                                                 int32_t column) {
    std::vector<int32_t> const starts = Line_starts(text);
    int32_t const clamped_line =
        std::clamp(line, 0, static_cast<int32_t>(starts.size()) - 1);
    int32_t const start = starts[static_cast<size_t>(clamped_line)];
    int32_t const end = Line_end(text, start);
    return std::clamp(start + column, start, end);
}

class FakeTextLayoutEngine final : public ITextLayoutEngine {
  public:
    [[nodiscard]] DraftTextLayoutResult Build_draft_layout(TextDraftBuffer const &buf,
                                                           PointPx origin) override {
        std::wstring const text = Flatten_text(buf.runs);
        DraftTextLayoutResult result{};
        std::vector<int32_t> const starts = Line_starts(text);

        int32_t max_line_length = 0;
        for (int32_t const start : starts) {
            max_line_length = std::max(max_line_length, Line_end(text, start) - start);
        }
        if (!text.empty()) {
            result.visual_bounds = RectPx::From_ltrb(
                origin.x, origin.y, origin.x + max_line_length * kCharWidthPx,
                origin.y + static_cast<int32_t>(starts.size()) * kLineHeightPx);
        }

        int32_t const active_line =
            Line_index_for_offset(text, buf.selection.active_utf16);
        int32_t const active_col = Column_for_offset(text, buf.selection.active_utf16);
        int32_t const caret_left = origin.x + active_col * kCharWidthPx;
        int32_t const caret_top = origin.y + active_line * kLineHeightPx;
        result.caret_rect = RectPx::From_ltrb(caret_left, caret_top, caret_left + 1,
                                              caret_top + kLineHeightPx);
        result.overwrite_caret_rect =
            RectPx::From_ltrb(caret_left, caret_top, caret_left + kCharWidthPx,
                              caret_top + kLineHeightPx);
        result.preferred_x_px = active_col * kCharWidthPx;

        int32_t const selection_start =
            std::min(buf.selection.anchor_utf16, buf.selection.active_utf16);
        int32_t const selection_end =
            std::max(buf.selection.anchor_utf16, buf.selection.active_utf16);
        if (selection_start != selection_end) {
            int32_t const start_line = Line_index_for_offset(text, selection_start);
            int32_t const end_line = Line_index_for_offset(text, selection_end);
            for (int32_t line = start_line; line <= end_line; ++line) {
                int32_t const line_start = starts[static_cast<size_t>(line)];
                int32_t const line_end = Line_end(text, line_start);
                int32_t const sel_left_offset =
                    (line == start_line) ? selection_start : line_start;
                int32_t const sel_right_offset =
                    (line == end_line) ? selection_end : line_end;
                if (sel_left_offset == sel_right_offset) {
                    continue;
                }
                int32_t const left =
                    origin.x + (sel_left_offset - line_start) * kCharWidthPx;
                int32_t const right =
                    origin.x + (sel_right_offset - line_start) * kCharWidthPx;
                int32_t const top = origin.y + line * kLineHeightPx;
                result.selection_rects.push_back(
                    RectPx::From_ltrb(left, top, right, top + kLineHeightPx));
            }
        }

        return result;
    }

    [[nodiscard]] int32_t Hit_test_point(TextDraftBuffer const &buf, PointPx origin,
                                         PointPx point) override {
        std::wstring const text = Flatten_text(buf.runs);
        std::vector<int32_t> const starts = Line_starts(text);
        int32_t const line = std::clamp((point.y - origin.y) / kLineHeightPx, 0,
                                        static_cast<int32_t>(starts.size()) - 1);
        int32_t const line_start = starts[static_cast<size_t>(line)];
        int32_t const line_end = Line_end(text, line_start);
        int32_t const column = std::max(0, (point.x - origin.x) / kCharWidthPx);
        return std::clamp(line_start + column, line_start, line_end);
    }

    [[nodiscard]] int32_t Move_vertical(TextDraftBuffer const &buf, PointPx origin,
                                        int32_t offset, int delta_lines,
                                        int32_t preferred_x_px) override {
        (void)origin;
        std::wstring const text = Flatten_text(buf.runs);
        std::vector<int32_t> const starts = Line_starts(text);
        int32_t const current_line = Line_index_for_offset(text, offset);
        int32_t const target_line = std::clamp(current_line + delta_lines, 0,
                                               static_cast<int32_t>(starts.size()) - 1);
        int32_t const column = std::max(0, preferred_x_px / kCharWidthPx);
        return Offset_for_line_and_column(text, target_line, column);
    }

    void Rasterize(TextAnnotation &annotation) override {
        annotation.bitmap_width_px = std::max(0, annotation.visual_bounds.Width());
        annotation.bitmap_height_px = std::max(0, annotation.visual_bounds.Height());
        annotation.bitmap_row_bytes = annotation.bitmap_width_px * 4;
        annotation.premultiplied_bgra.assign(
            static_cast<size_t>(annotation.bitmap_row_bytes) *
                static_cast<size_t>(annotation.bitmap_height_px),
            0);
    }

    void Rasterize_bubble(BubbleAnnotation &annotation) override {
        int32_t const d = annotation.diameter_px;
        annotation.bitmap_width_px = d;
        annotation.bitmap_height_px = d;
        annotation.bitmap_row_bytes = d * 4;
        annotation.premultiplied_bgra.assign(
            static_cast<size_t>(d) * static_cast<size_t>(d) * 4u, 0);
    }
};

[[nodiscard]] TextAnnotationBaseStyle Default_style() {
    return TextAnnotationBaseStyle{RGB(0x11, 0x22, 0x33), TextFontChoice::Sans, 12};
}

[[nodiscard]] TextEditController Make_controller(FakeTextLayoutEngine &engine) {
    return TextEditController({100, 200}, Default_style(), &engine);
}

} // namespace

TEST(text_draft_buffer, TextInput_InsertsAndReplacesSelection) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"abc");
    ASSERT_NE(controller.Build_view().annotation, nullptr);
    EXPECT_EQ(Flatten_text(controller.Build_view().annotation->runs), L"abc");

    controller.On_select_all();
    controller.On_text_input(L"z");

    ASSERT_NE(controller.Build_view().annotation, nullptr);
    EXPECT_EQ(Flatten_text(controller.Build_view().annotation->runs), L"z");
}

TEST(text_draft_buffer, Navigation_ExtendsSelectionAndCopiesSelectedText) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"abcd");
    controller.On_navigation(TextNavigationAction::Left, true);
    controller.On_navigation(TextNavigationAction::Left, true);

    EXPECT_EQ(controller.Copy_selected_text(), L"cd");
}

TEST(text_draft_buffer, Navigation_WordLineAndDocumentMovesCaret) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"ab cd");
    controller.On_navigation(TextNavigationAction::WordLeft, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 130);

    controller.On_navigation(TextNavigationAction::Home, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 100);

    controller.On_navigation(TextNavigationAction::End, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 150);

    controller.On_navigation(TextNavigationAction::DocHome, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 100);

    controller.On_navigation(TextNavigationAction::DocEnd, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 150);
}

TEST(text_draft_buffer, Navigation_LeftRightWordRightUpAndPagingMoveCaret) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"ab");
    controller.On_navigation(TextNavigationAction::DocHome, false);
    controller.On_navigation(TextNavigationAction::Right, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 110);

    controller.On_navigation(TextNavigationAction::Left, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 100);

    controller = Make_controller(engine);
    controller.On_text_input(L"ab cd");
    controller.On_navigation(TextNavigationAction::DocHome, false);
    controller.On_navigation(TextNavigationAction::WordRight, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 130);

    controller = Make_controller(engine);
    controller.On_text_input(L"abc\nxy");
    controller.On_navigation(TextNavigationAction::DocHome, false);
    controller.On_navigation(TextNavigationAction::Right, false);
    controller.On_navigation(TextNavigationAction::Down, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 110);
    EXPECT_EQ(controller.Build_view().caret_rect.top, 220);

    controller.On_navigation(TextNavigationAction::Up, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 110);
    EXPECT_EQ(controller.Build_view().caret_rect.top, 200);

    controller.On_navigation(TextNavigationAction::PgDn, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 120);
    EXPECT_EQ(controller.Build_view().caret_rect.top, 220);

    controller.On_navigation(TextNavigationAction::PgUp, false);
    EXPECT_EQ(controller.Build_view().caret_rect.left, 100);
    EXPECT_EQ(controller.Build_view().caret_rect.top, 200);
}

TEST(text_draft_buffer, Navigation_MoveVerticalPreservesPreferredColumn) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"abc\nxy");
    controller.On_navigation(TextNavigationAction::DocHome, false);
    controller.On_navigation(TextNavigationAction::Right, false);
    controller.On_navigation(TextNavigationAction::Right, false);
    controller.On_navigation(TextNavigationAction::Down, false);

    EXPECT_EQ(controller.Build_view().caret_rect.left, 120);
    EXPECT_EQ(controller.Build_view().caret_rect.top, 220);
}

TEST(text_draft_buffer, Navigation_ShiftExtendsSelectionForAllActions) {
    auto selected_after = [](std::wstring_view text,
                             std::initializer_list<TextNavigationAction> setup_actions,
                             TextNavigationAction action) {
        FakeTextLayoutEngine engine;
        TextEditController controller = Make_controller(engine);
        controller.On_text_input(text);
        for (TextNavigationAction const step : setup_actions) {
            controller.On_navigation(step, false);
        }
        controller.On_navigation(action, true);
        return controller.Copy_selected_text();
    };

    EXPECT_EQ(selected_after(L"abcd", {TextNavigationAction::DocEnd},
                             TextNavigationAction::Left),
              L"d");
    EXPECT_EQ(selected_after(L"abcd", {TextNavigationAction::DocHome},
                             TextNavigationAction::Right),
              L"a");
    EXPECT_EQ(
        selected_after(L"abc\nxy",
                       {TextNavigationAction::DocHome, TextNavigationAction::Right},
                       TextNavigationAction::Down),
        L"bc\nx");
    EXPECT_EQ(selected_after(L"abc\nxy",
                             {TextNavigationAction::DocHome,
                              TextNavigationAction::Right, TextNavigationAction::Down},
                             TextNavigationAction::Up),
              L"bc\nx");
    EXPECT_EQ(selected_after(L"ab cd", {TextNavigationAction::DocEnd},
                             TextNavigationAction::Home),
              L"ab cd");
    EXPECT_EQ(selected_after(L"ab cd", {TextNavigationAction::DocHome},
                             TextNavigationAction::End),
              L"ab cd");
    EXPECT_EQ(selected_after(L"abc\nxy", {TextNavigationAction::DocEnd},
                             TextNavigationAction::PgUp),
              L"abc\nxy");
    EXPECT_EQ(selected_after(L"abc\nxy", {TextNavigationAction::DocHome},
                             TextNavigationAction::PgDn),
              L"abc\nxy");
    EXPECT_EQ(selected_after(L"ab cd", {TextNavigationAction::DocEnd},
                             TextNavigationAction::WordLeft),
              L"cd");
    EXPECT_EQ(selected_after(L"ab cd", {TextNavigationAction::DocHome},
                             TextNavigationAction::WordRight),
              L"ab ");
    EXPECT_EQ(selected_after(L"ab cd", {TextNavigationAction::DocEnd},
                             TextNavigationAction::DocHome),
              L"ab cd");
    EXPECT_EQ(selected_after(L"ab cd", {TextNavigationAction::DocHome},
                             TextNavigationAction::DocEnd),
              L"ab cd");
}

TEST(text_draft_buffer, BackspaceAndDeleteSupportSingleCharAndWordOperations) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"abc def");
    controller.On_backspace(false);
    EXPECT_EQ(Flatten_text(controller.Build_view().annotation->runs), L"abc de");

    controller.On_backspace(true);
    EXPECT_EQ(Flatten_text(controller.Build_view().annotation->runs), L"abc ");

    controller.On_navigation(TextNavigationAction::DocHome, false);
    controller.On_delete(true);
    EXPECT_EQ(Flatten_text(controller.Build_view().annotation->runs), L"");
}

TEST(text_draft_buffer, SelectAllCutAndPasteNormalizeNewlines) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"alpha");
    controller.On_select_all();
    EXPECT_EQ(controller.Copy_selected_text(), L"alpha");
    EXPECT_EQ(controller.Cut_selected_text(), L"alpha");
    EXPECT_EQ(Flatten_text(controller.Build_view().annotation->runs), L"");

    controller.Paste_text(L"one\r\ntwo\rthree");
    controller.On_select_all();
    EXPECT_EQ(controller.Copy_selected_text(), L"one\ntwo\nthree");
}

TEST(text_draft_buffer, ToggleStyle_UpdatesTypingStyleAndSelectedRangeRuns) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.Toggle_style(TextStyleToggle::Italic);
    controller.On_text_input(L"a");
    ASSERT_EQ(controller.Build_view().annotation->runs.size(), 1u);
    EXPECT_TRUE(controller.Build_view().annotation->runs[0].flags.italic);

    controller.On_text_input(L"bc");
    controller.On_select_all();
    controller.Toggle_style(TextStyleToggle::Bold);
    ASSERT_EQ(controller.Build_view().annotation->runs.size(), 1u);
    EXPECT_TRUE(controller.Build_view().annotation->runs[0].flags.bold);

    controller.Toggle_style(TextStyleToggle::Bold);
    ASSERT_EQ(controller.Build_view().annotation->runs.size(), 1u);
    EXPECT_FALSE(controller.Build_view().annotation->runs[0].flags.bold);
}

TEST(text_draft_buffer, ToggleStyle_SelectedRangeSplitsAndMergesRuns) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"abcd");
    controller.On_navigation(TextNavigationAction::DocHome, false);
    controller.On_navigation(TextNavigationAction::Right, false);
    controller.On_navigation(TextNavigationAction::Right, true);
    controller.On_navigation(TextNavigationAction::Right, true);
    controller.Toggle_style(TextStyleToggle::Bold);

    ASSERT_EQ(controller.Build_view().annotation->runs.size(), 3u);
    EXPECT_EQ(controller.Build_view().annotation->runs[0].text, L"a");
    EXPECT_FALSE(controller.Build_view().annotation->runs[0].flags.bold);
    EXPECT_EQ(controller.Build_view().annotation->runs[1].text, L"bc");
    EXPECT_TRUE(controller.Build_view().annotation->runs[1].flags.bold);
    EXPECT_EQ(controller.Build_view().annotation->runs[2].text, L"d");
    EXPECT_FALSE(controller.Build_view().annotation->runs[2].flags.bold);

    controller.Toggle_style(TextStyleToggle::Bold);

    ASSERT_EQ(controller.Build_view().annotation->runs.size(), 1u);
    EXPECT_EQ(controller.Build_view().annotation->runs[0].text, L"abcd");
    EXPECT_FALSE(controller.Build_view().annotation->runs[0].flags.bold);
}

TEST(text_draft_buffer, UndoRedo_RollBackMutationsButNotNavigationSnapshots) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"abc");
    controller.On_navigation(TextNavigationAction::Left, false);
    controller.Undo();
    EXPECT_EQ(Flatten_text(controller.Build_view().annotation->runs), L"");

    controller.Redo();
    EXPECT_EQ(Flatten_text(controller.Build_view().annotation->runs), L"abc");
}

TEST(text_draft_buffer, OverwriteMode_ReplacesWithinLineAndFallsBackAtLineEnd) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"ab\nc");
    controller.On_navigation(TextNavigationAction::DocHome, false);
    controller.On_navigation(TextNavigationAction::Right, false);
    controller.Toggle_insert_mode();
    controller.On_text_input(L"Z");
    EXPECT_EQ(Flatten_text(controller.Build_view().annotation->runs), L"aZ\nc");

    controller.On_navigation(TextNavigationAction::End, false);
    controller.On_text_input(L"Q");
    EXPECT_EQ(Flatten_text(controller.Build_view().annotation->runs), L"aZQ\nc");
}

TEST(text_draft_buffer, PointerSelection_UsesLayoutHitTesting) {
    FakeTextLayoutEngine engine;
    TextEditController controller = Make_controller(engine);

    controller.On_text_input(L"abcd");
    controller.On_pointer_press({110, 200});
    controller.On_pointer_move({130, 200}, true);
    controller.On_pointer_release({130, 200});

    EXPECT_EQ(controller.Copy_selected_text(), L"bc");
}
