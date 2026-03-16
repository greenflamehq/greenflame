#pragma once

#include "greenflame_core/text_layout_engine.h"

namespace {

using namespace greenflame::core;

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

class FakeTextLayoutEngine final : public ITextLayoutEngine {
  public:
    [[nodiscard]] DraftTextLayoutResult Build_draft_layout(TextDraftBuffer const &buf,
                                                           PointPx origin) override {
        constexpr int32_t char_width_px = 10;
        constexpr int32_t line_height_px = 20;

        std::wstring const text = Flatten_text(buf.runs);
        DraftTextLayoutResult result{};
        std::vector<int32_t> const starts = Line_starts(text);

        int32_t max_line_length = 0;
        for (int32_t const start : starts) {
            max_line_length = std::max(max_line_length, Line_end(text, start) - start);
        }
        if (!text.empty()) {
            result.visual_bounds = RectPx::From_ltrb(
                origin.x, origin.y, origin.x + max_line_length * char_width_px,
                origin.y + static_cast<int32_t>(starts.size()) * line_height_px);
        }

        int32_t const active_line =
            Line_index_for_offset(text, buf.selection.active_utf16);
        int32_t const active_column = std::clamp(buf.selection.active_utf16, 0,
                                                 static_cast<int32_t>(text.size())) -
                                      starts[static_cast<size_t>(active_line)];
        int32_t const caret_left = origin.x + active_column * char_width_px;
        int32_t const caret_top = origin.y + active_line * line_height_px;
        result.caret_rect = RectPx::From_ltrb(caret_left, caret_top, caret_left + 1,
                                              caret_top + line_height_px);
        result.overwrite_caret_rect =
            RectPx::From_ltrb(caret_left, caret_top, caret_left + char_width_px,
                              caret_top + line_height_px);
        result.preferred_x_px = active_column * char_width_px;
        return result;
    }

    [[nodiscard]] int32_t Hit_test_point(TextDraftBuffer const &buf, PointPx origin,
                                         PointPx point) override {
        constexpr int32_t char_width_px = 10;
        constexpr int32_t line_height_px = 20;

        std::wstring const text = Flatten_text(buf.runs);
        std::vector<int32_t> const starts = Line_starts(text);
        int32_t const line = std::clamp((point.y - origin.y) / line_height_px, 0,
                                        static_cast<int32_t>(starts.size()) - 1);
        int32_t const line_start = starts[static_cast<size_t>(line)];
        int32_t const line_end = Line_end(text, line_start);
        int32_t const column = std::max(0, (point.x - origin.x) / char_width_px);
        return std::clamp(line_start + column, line_start, line_end);
    }

    [[nodiscard]] int32_t Move_vertical(TextDraftBuffer const &buf, PointPx origin,
                                        int32_t offset, int delta_lines,
                                        int32_t preferred_x_px) override {
        constexpr int32_t char_width_px = 10;
        (void)origin;

        std::wstring const text = Flatten_text(buf.runs);
        std::vector<int32_t> const starts = Line_starts(text);
        int32_t const current_line = Line_index_for_offset(text, offset);
        int32_t const target_line = std::clamp(current_line + delta_lines, 0,
                                               static_cast<int32_t>(starts.size()) - 1);
        int32_t const column = std::max(0, preferred_x_px / char_width_px);
        int32_t const line_start = starts[static_cast<size_t>(target_line)];
        return std::clamp(line_start + column, line_start, Line_end(text, line_start));
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

} // namespace
