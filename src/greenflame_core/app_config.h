#pragma once

#include "greenflame_core/color_wheel.h"
#include "greenflame_core/text_annotation_types.h"

namespace greenflame::core {

struct AppConfig final {
    static constexpr int32_t kDefaultFreehandSize = 2;
    static constexpr int32_t kDefaultLineSize = 2;
    static constexpr int32_t kDefaultArrowSize = 2;
    static constexpr int32_t kDefaultRectSize = 2;
    static constexpr int32_t kDefaultEllipseSize = 2;
    static constexpr int32_t kDefaultHighlighterSize = 10;
    static constexpr int32_t kDefaultBubbleSize = 10;
    static constexpr int32_t kDefaultTextSize = 10;
    static constexpr int32_t kDefaultToolSizeOverlayDurationMs = 800;
    static constexpr int32_t kDefaultHighlighterPauseStraightenMs = 800;

    std::wstring default_save_dir = {};
    std::wstring last_save_as_dir = {};
    std::wstring filename_pattern_region = {};
    std::wstring filename_pattern_desktop = {};
    std::wstring filename_pattern_monitor = {};
    std::wstring filename_pattern_window = {};
    std::wstring default_save_format = {}; // "png" (default), "jpg"/"jpeg", or "bmp".
    int32_t freehand_size = kDefaultFreehandSize;
    int32_t line_size = kDefaultLineSize;
    int32_t arrow_size = kDefaultArrowSize;
    int32_t rect_size = kDefaultRectSize;
    int32_t ellipse_size = kDefaultEllipseSize;
    int32_t highlighter_size = kDefaultHighlighterSize;
    int32_t bubble_size = kDefaultBubbleSize;
    int32_t text_size = kDefaultTextSize;
    AnnotationColorPalette annotation_colors = kDefaultAnnotationColorPalette;
    int32_t current_annotation_color_index = kDefaultAnnotationColorIndex;
    HighlighterColorPalette highlighter_colors = kDefaultHighlighterColorPalette;
    int32_t current_highlighter_color_index = kDefaultHighlighterColorIndex;
    int32_t highlighter_opacity_percent = kDefaultHighlighterOpacityPercent;
    int32_t highlighter_pause_straighten_ms = kDefaultHighlighterPauseStraightenMs;
    int32_t highlighter_pause_straighten_deadzone_px = 0;
    TextFontChoice text_current_font = TextFontChoice::Sans;
    TextFontChoice bubble_current_font = TextFontChoice::Sans;
    std::wstring text_font_sans = L"Arial";
    std::wstring text_font_serif = L"Times New Roman";
    std::wstring text_font_mono = L"Courier New";
    std::wstring text_font_art = L"Comic Sans MS";
    int32_t tool_size_overlay_duration_ms = kDefaultToolSizeOverlayDurationMs;
    bool show_balloons = true;
    bool show_selection_size_side_labels = true;
    bool show_selection_size_center_label = true;

    void Normalize();
};

} // namespace greenflame::core
