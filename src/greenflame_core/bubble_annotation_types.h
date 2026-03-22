#pragma once

#include "greenflame_core/rect_px.h"
#include "greenflame_core/text_annotation_types.h"

namespace greenflame::core {

// WCAG 2.x relative-luminance threshold: L > 0.179 → use black text, else white.
[[nodiscard]] COLORREF Bubble_text_color(COLORREF bg) noexcept;

struct BubbleAnnotation final {
    PointPx center = {};
    int32_t diameter_px = 0;
    COLORREF color = 0;
    TextFontChoice font_choice = TextFontChoice::Sans;
    std::wstring font_family = {};
    int32_t counter_value = 0;
    int32_t bitmap_width_px = 0;
    int32_t bitmap_height_px = 0;
    int32_t bitmap_row_bytes = 0;
    std::vector<uint8_t> premultiplied_bgra = {};

    bool operator==(BubbleAnnotation const &) const noexcept = default;
};

} // namespace greenflame::core
