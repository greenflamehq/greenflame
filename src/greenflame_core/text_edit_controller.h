#pragma once

#include "greenflame_core/text_layout_engine.h"

namespace greenflame::core {

enum class TextNavigationAction : uint8_t {
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PgUp,
    PgDn,
    WordLeft,
    WordRight,
    DocHome,
    DocEnd,
};

enum class TextStyleToggle : uint8_t {
    Bold,
    Italic,
    Underline,
    Strikethrough,
};

struct TextDraftView final {
    TextAnnotation const *annotation = nullptr;
    RectPx visual_bounds = {};
    std::vector<RectPx> selection_rects = {};
    RectPx caret_rect = {};
    RectPx overwrite_caret_rect = {};
    bool insert_mode = true;

    [[nodiscard]] RectPx Hit_bounds() const noexcept {
        RectPx bounds = visual_bounds;
        if (!caret_rect.Is_empty()) {
            bounds = RectPx::Union(bounds, caret_rect);
        }
        if (!overwrite_caret_rect.Is_empty()) {
            bounds = RectPx::Union(bounds, overwrite_caret_rect);
        }
        return bounds;
    }
};

class TextEditController final {
  public:
    TextEditController(PointPx origin, TextAnnotationBaseStyle const &base_style,
                       ITextLayoutEngine *layout_engine);
    TextEditController(TextEditController const &) = delete;
    TextEditController &operator=(TextEditController const &) = delete;
    TextEditController(TextEditController &&) = default;
    TextEditController &operator=(TextEditController &&) = default;

    [[nodiscard]] TextDraftView Build_view() const;
    void On_text_input(std::wstring_view text);
    void On_select_all();
    void On_navigation(TextNavigationAction action, bool extend_selection);
    void On_backspace(bool by_word);
    void On_delete(bool by_word);
    void Toggle_style(TextStyleToggle which);
    void Toggle_insert_mode() noexcept;
    [[nodiscard]] std::wstring Copy_selected_text() const;
    [[nodiscard]] std::wstring Cut_selected_text();
    void Paste_text(std::wstring_view text);
    // Returns the selected runs with their TextStyleFlags preserved.
    // Returns an empty vector when there is no selection.
    [[nodiscard]] std::vector<TextRun> Copy_selected_runs() const;
    // Replaces the current selection with the given runs, applying each run's own
    // TextStyleFlags. After insertion, typing_style.flags is updated to match the
    // last inserted character so that continued typing uses the pasted style.
    void Paste_runs(std::span<const TextRun> runs);
    void Undo();
    void Redo();
    void On_pointer_press(PointPx cursor);
    void On_pointer_move(PointPx cursor, bool primary_down);
    void On_pointer_release(PointPx cursor);
    [[nodiscard]] TextAnnotation Commit() const;
    void Cancel() noexcept;

  private:
    void Rebuild_layout();
    void Refresh_preferred_x_from_layout() noexcept;
    void Push_snapshot_if_changed();
    void Replace_selection_with_text(std::wstring_view text, bool allow_overwrite);
    void Delete_selected_range();
    [[nodiscard]] int32_t Current_text_length() const;
    [[nodiscard]] int32_t Hit_test_offset(PointPx cursor) const;
    [[nodiscard]] int32_t Move_vertical(int32_t offset, int delta_lines) const;

    TextDraftBuffer buffer_ = {};
    PointPx origin_ = {};
    std::vector<TextDraftSnapshot> history_ = {};
    size_t history_index_ = 0;
    ITextLayoutEngine *layout_engine_ = nullptr;
    DraftTextLayoutResult layout_ = {};
    TextAnnotation draft_annotation_ = {};
    bool pointer_selecting_ = false;
};

} // namespace greenflame::core
