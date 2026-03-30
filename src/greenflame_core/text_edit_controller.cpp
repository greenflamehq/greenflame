#include "greenflame_core/text_edit_controller.h"

namespace greenflame::core {

namespace {

struct StyledCodeUnit final {
    wchar_t code_unit = L'\0';
    TextStyleFlags flags = {};
};

[[nodiscard]] std::vector<StyledCodeUnit> Expand_runs(std::span<const TextRun> runs) {
    std::vector<StyledCodeUnit> code_units;
    size_t total_size = 0;
    for (TextRun const &run : runs) {
        total_size += run.text.size();
    }
    code_units.reserve(total_size);

    for (TextRun const &run : runs) {
        for (wchar_t const code_unit : run.text) {
            code_units.push_back(StyledCodeUnit{code_unit, run.flags});
        }
    }
    return code_units;
}

[[nodiscard]] std::vector<TextRun>
Collapse_runs(std::span<const StyledCodeUnit> code_units) {
    std::vector<TextRun> runs;
    for (StyledCodeUnit const &code_unit : code_units) {
        if (!runs.empty() && runs.back().flags == code_unit.flags) {
            runs.back().text.push_back(code_unit.code_unit);
            continue;
        }
        runs.push_back(TextRun{std::wstring(1, code_unit.code_unit), code_unit.flags});
    }
    return runs;
}

[[nodiscard]] std::wstring Flatten_text(std::span<const TextRun> runs) {
    std::wstring text;
    size_t total_size = 0;
    for (TextRun const &run : runs) {
        total_size += run.text.size();
    }
    text.reserve(total_size);

    for (TextRun const &run : runs) {
        text += run.text;
    }
    return text;
}

[[nodiscard]] int32_t Clamp_offset(std::wstring_view text, int32_t offset) noexcept {
    return std::clamp(offset, 0, static_cast<int32_t>(text.size()));
}

[[nodiscard]] int32_t Selection_start(TextSelection const &selection) noexcept {
    return std::min(selection.anchor_utf16, selection.active_utf16);
}

[[nodiscard]] int32_t Selection_end(TextSelection const &selection) noexcept {
    return std::max(selection.anchor_utf16, selection.active_utf16);
}

[[nodiscard]] bool Has_selection(TextSelection const &selection) noexcept {
    return selection.anchor_utf16 != selection.active_utf16;
}

[[nodiscard]] bool Is_word_char(wchar_t ch) noexcept {
    return std::iswalnum(ch) != 0 || ch == L'_';
}

[[nodiscard]] int32_t Find_line_start(std::wstring_view text, int32_t offset) noexcept {
    int32_t line_start = Clamp_offset(text, offset);
    while (line_start > 0 && text[static_cast<size_t>(line_start - 1)] != L'\n') {
        --line_start;
    }
    return line_start;
}

[[nodiscard]] int32_t Find_line_end(std::wstring_view text, int32_t offset) noexcept {
    int32_t line_end = Clamp_offset(text, offset);
    while (line_end < static_cast<int32_t>(text.size()) &&
           text[static_cast<size_t>(line_end)] != L'\n') {
        ++line_end;
    }
    return line_end;
}

[[nodiscard]] int32_t Find_word_left(std::wstring_view text, int32_t offset) noexcept {
    int32_t pos = Clamp_offset(text, offset);
    while (pos > 0 && !Is_word_char(text[static_cast<size_t>(pos - 1)])) {
        --pos;
    }
    while (pos > 0 && Is_word_char(text[static_cast<size_t>(pos - 1)])) {
        --pos;
    }
    return pos;
}

[[nodiscard]] int32_t Find_word_right(std::wstring_view text, int32_t offset) noexcept {
    int32_t pos = Clamp_offset(text, offset);
    while (pos < static_cast<int32_t>(text.size()) &&
           Is_word_char(text[static_cast<size_t>(pos)])) {
        ++pos;
    }
    while (pos < static_cast<int32_t>(text.size()) &&
           !Is_word_char(text[static_cast<size_t>(pos)])) {
        ++pos;
    }
    return pos;
}

[[nodiscard]] bool Get_style_flag(TextStyleFlags const &flags,
                                  TextStyleToggle which) noexcept {
    switch (which) {
    case TextStyleToggle::Bold:
        return flags.bold;
    case TextStyleToggle::Italic:
        return flags.italic;
    case TextStyleToggle::Underline:
        return flags.underline;
    case TextStyleToggle::Strikethrough:
        return flags.strikethrough;
    }
    return false;
}

void Set_style_flag(TextStyleFlags &flags, TextStyleToggle which, bool value) noexcept {
    switch (which) {
    case TextStyleToggle::Bold:
        flags.bold = value;
        break;
    case TextStyleToggle::Italic:
        flags.italic = value;
        break;
    case TextStyleToggle::Underline:
        flags.underline = value;
        break;
    case TextStyleToggle::Strikethrough:
        flags.strikethrough = value;
        break;
    }
}

[[nodiscard]] std::wstring Normalize_newlines(std::wstring_view text) {
    std::wstring normalized;
    normalized.reserve(text.size());
    for (size_t index = 0; index < text.size(); ++index) {
        wchar_t const code_unit = text[index];
        if (code_unit == L'\r') {
            normalized.push_back(L'\n');
            if (index + 1 < text.size() && text[index + 1] == L'\n') {
                ++index;
            }
            continue;
        }
        normalized.push_back(code_unit);
    }
    return normalized;
}

} // namespace

TextEditController::TextEditController(PointPx origin,
                                       TextAnnotationBaseStyle const &base_style,
                                       ITextLayoutEngine *layout_engine)
    : origin_(origin), layout_engine_(layout_engine) {
    buffer_.base_style = base_style;
    draft_annotation_.origin = origin_;
    draft_annotation_.base_style = base_style;
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
    history_.push_back(TextDraftSnapshot{buffer_});
}

TextDraftView TextEditController::Build_view() const {
    return TextDraftView{&draft_annotation_,           layout_.visual_bounds,
                         layout_.selection_rects,      layout_.caret_rect,
                         layout_.overwrite_caret_rect, !buffer_.overwrite_mode};
}

void TextEditController::On_text_input(std::wstring_view text) {
    Replace_selection_with_text(text, true);
}

void TextEditController::On_select_all() {
    int32_t const text_length = Current_text_length();
    if (buffer_.selection.anchor_utf16 == 0 &&
        buffer_.selection.active_utf16 == text_length) {
        return;
    }

    buffer_.selection = TextSelection{0, text_length};
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
}

void TextEditController::On_navigation(TextNavigationAction action,
                                       bool extend_selection) {
    std::wstring const text = Flatten_text(buffer_.runs);
    TextSelection const old_selection = buffer_.selection;
    int32_t const text_length = static_cast<int32_t>(text.size());
    int32_t const caret = Clamp_offset(text, buffer_.selection.active_utf16);
    int32_t next_offset = caret;
    bool preserve_preferred_x = false;
    bool collapsed_selection = false;

    if (!extend_selection && Has_selection(buffer_.selection) &&
        (action == TextNavigationAction::Left ||
         action == TextNavigationAction::Right ||
         action == TextNavigationAction::WordLeft ||
         action == TextNavigationAction::WordRight)) {
        collapsed_selection = true;
        next_offset = (action == TextNavigationAction::Left ||
                       action == TextNavigationAction::WordLeft)
                          ? Selection_start(buffer_.selection)
                          : Selection_end(buffer_.selection);
    }

    if (!collapsed_selection) {
        switch (action) {
        case TextNavigationAction::Left:
            next_offset = std::max(0, caret - 1);
            break;
        case TextNavigationAction::Right:
            next_offset = std::min(text_length, caret + 1);
            break;
        case TextNavigationAction::Up:
            next_offset = Move_vertical(caret, -1);
            preserve_preferred_x = true;
            break;
        case TextNavigationAction::Down:
            next_offset = Move_vertical(caret, 1);
            preserve_preferred_x = true;
            break;
        case TextNavigationAction::Home:
            next_offset = Find_line_start(text, caret);
            break;
        case TextNavigationAction::End:
            next_offset = Find_line_end(text, caret);
            break;
        case TextNavigationAction::PgUp:
        case TextNavigationAction::DocHome:
            next_offset = 0;
            break;
        case TextNavigationAction::PgDn:
        case TextNavigationAction::DocEnd:
            next_offset = text_length;
            break;
        case TextNavigationAction::WordLeft:
            next_offset = Find_word_left(text, caret);
            break;
        case TextNavigationAction::WordRight:
            next_offset = Find_word_right(text, caret);
            break;
        }
    }

    if (extend_selection) {
        buffer_.selection.active_utf16 = next_offset;
    } else {
        buffer_.selection = TextSelection{next_offset, next_offset};
    }

    if (buffer_.selection == old_selection) {
        return;
    }

    int32_t const preserved_x = buffer_.preferred_x_px;
    Rebuild_layout();
    if (preserve_preferred_x) {
        buffer_.preferred_x_px = preserved_x;
    } else {
        Refresh_preferred_x_from_layout();
    }
}

void TextEditController::On_backspace(bool by_word) {
    std::wstring const text = Flatten_text(buffer_.runs);
    int32_t delete_start = Selection_start(buffer_.selection);
    if (!Has_selection(buffer_.selection)) {
        int32_t const caret = Clamp_offset(text, buffer_.selection.active_utf16);
        int32_t const delete_end = caret;
        delete_start = by_word ? Find_word_left(text, caret) : std::max(0, caret - 1);
        if (delete_start == delete_end) {
            return;
        }
        buffer_.selection = TextSelection{delete_start, delete_end};
    }

    Delete_selected_range();
    buffer_.selection = TextSelection{delete_start, delete_start};
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
    Push_snapshot_if_changed();
}

void TextEditController::On_delete(bool by_word) {
    std::wstring const text = Flatten_text(buffer_.runs);
    int32_t delete_start = Selection_start(buffer_.selection);
    if (!Has_selection(buffer_.selection)) {
        int32_t const caret = Clamp_offset(text, buffer_.selection.active_utf16);
        int32_t const delete_end =
            by_word ? Find_word_right(text, caret)
                    : std::min(static_cast<int32_t>(text.size()), caret + 1);
        delete_start = caret;
        if (delete_start == delete_end) {
            return;
        }
        buffer_.selection = TextSelection{delete_start, delete_end};
    }

    Delete_selected_range();
    buffer_.selection = TextSelection{delete_start, delete_start};
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
    Push_snapshot_if_changed();
}

void TextEditController::Toggle_style(TextStyleToggle which) {
    if (!Has_selection(buffer_.selection)) {
        TextStyleFlags const old_flags = buffer_.typing_style.flags;
        Set_style_flag(buffer_.typing_style.flags, which,
                       !Get_style_flag(buffer_.typing_style.flags, which));
        if (buffer_.typing_style.flags == old_flags) {
            return;
        }
        Rebuild_layout();
        Refresh_preferred_x_from_layout();
        Push_snapshot_if_changed();
        return;
    }

    std::vector<StyledCodeUnit> code_units = Expand_runs(buffer_.runs);
    int32_t const start = Selection_start(buffer_.selection);
    int32_t const end = Selection_end(buffer_.selection);
    bool all_enabled = true;
    for (int32_t offset = start; offset < end; ++offset) {
        all_enabled =
            all_enabled &&
            Get_style_flag(code_units[static_cast<size_t>(offset)].flags, which);
    }

    for (int32_t offset = start; offset < end; ++offset) {
        Set_style_flag(code_units[static_cast<size_t>(offset)].flags, which,
                       !all_enabled);
    }

    buffer_.runs = Collapse_runs(code_units);
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
    Push_snapshot_if_changed();
}

void TextEditController::Toggle_insert_mode() noexcept {
    buffer_.overwrite_mode = !buffer_.overwrite_mode;
    Rebuild_layout();
}

std::wstring TextEditController::Copy_selected_text() const {
    if (!Has_selection(buffer_.selection)) {
        return {};
    }

    std::wstring const text = Flatten_text(buffer_.runs);
    int32_t const start = Selection_start(buffer_.selection);
    int32_t const length = Selection_end(buffer_.selection) - start;
    return text.substr(static_cast<size_t>(start), static_cast<size_t>(length));
}

std::wstring TextEditController::Cut_selected_text() {
    std::wstring selected_text = Copy_selected_text();
    if (selected_text.empty()) {
        return {};
    }

    int32_t const start = Selection_start(buffer_.selection);
    Delete_selected_range();
    buffer_.selection = TextSelection{start, start};
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
    Push_snapshot_if_changed();
    return selected_text;
}

void TextEditController::Paste_text(std::wstring_view text) {
    std::wstring const normalized = Normalize_newlines(text);
    if (normalized.empty()) {
        return;
    }
    Replace_selection_with_text(normalized, false);
}

std::vector<TextRun> TextEditController::Copy_selected_runs() const {
    if (!Has_selection(buffer_.selection)) {
        return {};
    }

    std::vector<StyledCodeUnit> const all_units = Expand_runs(buffer_.runs);
    int32_t const start = Selection_start(buffer_.selection);
    int32_t const end = Selection_end(buffer_.selection);
    return Collapse_runs(std::span<const StyledCodeUnit>(all_units).subspan(
        static_cast<size_t>(start), static_cast<size_t>(end - start)));
}

void TextEditController::Paste_runs(std::span<const TextRun> runs) {
    if (runs.empty()) {
        return;
    }

    // Normalize newlines and flatten the source runs to styled code units.
    std::vector<StyledCodeUnit> normalized_units;
    for (TextRun const &run : runs) {
        std::wstring const text = Normalize_newlines(run.text);
        for (wchar_t const ch : text) {
            normalized_units.push_back(StyledCodeUnit{ch, run.flags});
        }
    }
    if (normalized_units.empty()) {
        return;
    }

    // Splice into the buffer, replacing the current selection.
    std::vector<StyledCodeUnit> code_units = Expand_runs(buffer_.runs);
    int32_t insert_offset = Selection_start(buffer_.selection);
    int32_t const erase_end = Selection_end(buffer_.selection);
    if (erase_end > insert_offset) {
        code_units.erase(code_units.begin() +
                             static_cast<std::ptrdiff_t>(insert_offset),
                         code_units.begin() + static_cast<std::ptrdiff_t>(erase_end));
    }
    code_units.insert(code_units.begin() + static_cast<std::ptrdiff_t>(insert_offset),
                      normalized_units.begin(), normalized_units.end());
    insert_offset += static_cast<int32_t>(normalized_units.size());

    buffer_.runs = Collapse_runs(code_units);
    // Update typing_style so that continued typing after the paste uses the
    // same formatting as the last pasted character.
    buffer_.typing_style.flags = normalized_units.back().flags;
    buffer_.selection = TextSelection{insert_offset, insert_offset};
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
    Push_snapshot_if_changed();
}

void TextEditController::Undo() {
    if (history_index_ == 0) {
        return;
    }

    --history_index_;
    buffer_ = history_[history_index_].buffer;
    pointer_selecting_ = false;
    Rebuild_layout();
}

void TextEditController::Redo() {
    if (history_index_ + 1 >= history_.size()) {
        return;
    }

    ++history_index_;
    buffer_ = history_[history_index_].buffer;
    pointer_selecting_ = false;
    Rebuild_layout();
}

void TextEditController::On_pointer_press(PointPx cursor) {
    int32_t const hit_offset = Hit_test_offset(cursor);
    pointer_selecting_ = true;
    if (buffer_.selection.anchor_utf16 == hit_offset &&
        buffer_.selection.active_utf16 == hit_offset) {
        return;
    }

    buffer_.selection = TextSelection{hit_offset, hit_offset};
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
}

void TextEditController::On_pointer_move(PointPx cursor, bool primary_down) {
    if (!pointer_selecting_ || !primary_down) {
        return;
    }

    int32_t const hit_offset = Hit_test_offset(cursor);
    if (buffer_.selection.active_utf16 == hit_offset) {
        return;
    }

    buffer_.selection.active_utf16 = hit_offset;
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
}

void TextEditController::On_pointer_release(PointPx cursor) {
    if (!pointer_selecting_) {
        return;
    }

    int32_t const hit_offset = Hit_test_offset(cursor);
    pointer_selecting_ = false;
    if (buffer_.selection.active_utf16 == hit_offset) {
        return;
    }

    buffer_.selection.active_utf16 = hit_offset;
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
}

TextAnnotation TextEditController::Commit() const {
    TextAnnotation annotation = draft_annotation_;
    annotation.premultiplied_bgra.clear();
    annotation.bitmap_width_px = 0;
    annotation.bitmap_height_px = 0;
    annotation.bitmap_row_bytes = 0;
    return annotation;
}

void TextEditController::Cancel() noexcept { pointer_selecting_ = false; }

void TextEditController::Rebuild_layout() {
    draft_annotation_.origin = origin_;
    draft_annotation_.base_style = buffer_.base_style;
    draft_annotation_.runs = buffer_.runs;
    draft_annotation_.bitmap_width_px = 0;
    draft_annotation_.bitmap_height_px = 0;
    draft_annotation_.bitmap_row_bytes = 0;
    draft_annotation_.premultiplied_bgra.clear();

    if (layout_engine_ != nullptr) {
        layout_ = layout_engine_->Build_draft_layout(buffer_, origin_);
    } else {
        layout_ = {};
    }
    draft_annotation_.visual_bounds = layout_.visual_bounds;
}

void TextEditController::Refresh_preferred_x_from_layout() noexcept {
    buffer_.preferred_x_px = layout_.preferred_x_px;
}

void TextEditController::Push_snapshot_if_changed() {
    if (!history_.empty() && history_[history_index_].buffer == buffer_) {
        return;
    }

    history_.resize(history_index_ + 1);
    history_.push_back(TextDraftSnapshot{buffer_});
    history_index_ = history_.size() - 1;
}

void TextEditController::Replace_selection_with_text(std::wstring_view text,
                                                     bool allow_overwrite) {
    if (text.empty()) {
        return;
    }

    std::vector<StyledCodeUnit> code_units = Expand_runs(buffer_.runs);
    int32_t insert_offset = Selection_start(buffer_.selection);
    int32_t const erase_end = Selection_end(buffer_.selection);
    bool const had_selection = erase_end > insert_offset;
    if (erase_end > insert_offset) {
        code_units.erase(code_units.begin() +
                             static_cast<std::ptrdiff_t>(insert_offset),
                         code_units.begin() + static_cast<std::ptrdiff_t>(erase_end));
    }

    for (wchar_t const code_unit : text) {
        StyledCodeUnit const inserted{code_unit, buffer_.typing_style.flags};
        if (allow_overwrite && buffer_.overwrite_mode && !had_selection &&
            code_unit != L'\n' &&
            insert_offset < static_cast<int32_t>(code_units.size()) &&
            code_units[static_cast<size_t>(insert_offset)].code_unit != L'\n') {
            code_units[static_cast<size_t>(insert_offset)] = inserted;
        } else {
            code_units.insert(code_units.begin() +
                                  static_cast<std::ptrdiff_t>(insert_offset),
                              inserted);
        }
        ++insert_offset;
    }

    buffer_.runs = Collapse_runs(code_units);
    buffer_.selection = TextSelection{insert_offset, insert_offset};
    Rebuild_layout();
    Refresh_preferred_x_from_layout();
    Push_snapshot_if_changed();
}

void TextEditController::Delete_selected_range() {
    int32_t const start = Selection_start(buffer_.selection);
    int32_t const end = Selection_end(buffer_.selection);
    if (start == end) {
        return;
    }

    std::vector<StyledCodeUnit> code_units = Expand_runs(buffer_.runs);
    code_units.erase(code_units.begin() + static_cast<std::ptrdiff_t>(start),
                     code_units.begin() + static_cast<std::ptrdiff_t>(end));
    buffer_.runs = Collapse_runs(code_units);
}

int32_t TextEditController::Current_text_length() const {
    int32_t total_length = 0;
    for (TextRun const &run : buffer_.runs) {
        total_length += static_cast<int32_t>(run.text.size());
    }
    return total_length;
}

int32_t TextEditController::Hit_test_offset(PointPx cursor) const {
    if (layout_engine_ == nullptr) {
        return 0;
    }
    return std::clamp(layout_engine_->Hit_test_point(buffer_, origin_, cursor), 0,
                      Current_text_length());
}

int32_t TextEditController::Move_vertical(int32_t offset, int delta_lines) const {
    if (layout_engine_ == nullptr) {
        return offset;
    }
    return std::clamp(layout_engine_->Move_vertical(buffer_, origin_, offset,
                                                    delta_lines,
                                                    buffer_.preferred_x_px),
                      0, Current_text_length());
}

} // namespace greenflame::core
