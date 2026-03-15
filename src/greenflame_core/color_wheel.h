#pragma once

#include "greenflame_core/rect_px.h"
#include "greenflame_core/text_annotation_types.h"

namespace greenflame::core {

constexpr size_t kAnnotationColorSlotCount = 8;
constexpr size_t kHighlighterColorSlotCount = 6;

using AnnotationColorPalette = std::array<COLORREF, kAnnotationColorSlotCount>;
using HighlighterColorPalette = std::array<COLORREF, kHighlighterColorSlotCount>;

inline constexpr int32_t kColorWheelOuterDiameterPx = 130;
inline constexpr float kColorWheelWidthPx = 22.0f;
inline constexpr float kColorWheelSegmentGapPx = 8.0f;
inline constexpr float kColorWheelSegmentBorderWidthPx = 2.0f;
inline constexpr float kColorWheelSelectionHaloGapPx = 2.0f;
inline constexpr float kColorWheelSelectionHaloInnerWidthPx = 3.0f;
inline constexpr float kColorWheelSelectionHaloOuterWidthPx = 3.0f;
inline constexpr float kColorWheelHoverHaloInnerWidthPx = 5.0f;
inline constexpr float kColorWheelHoverHaloOuterWidthPx = 5.0f;

inline constexpr float kTextWheelHubGapPx = 8.0f;
inline constexpr float kTextWheelHubHalfGapPx = kTextWheelHubGapPx / 2.0f;
inline constexpr float kTextWheelHubRingGapPx = 8.0f;
inline constexpr float kTextWheelHubGlyphRectWidthPx = 20.0f;
inline constexpr float kTextWheelHubGlyphRectHeightPx = 12.0f;
inline constexpr bool kTextWheelHubDrawBorder = true;

static_assert(kColorWheelOuterDiameterPx > 0);
static_assert(kColorWheelWidthPx > 0.0f);
static_assert(kColorWheelWidthPx <
              static_cast<float>(kColorWheelOuterDiameterPx) / 2.0f);

[[nodiscard]] constexpr COLORREF Make_colorref(uint8_t red, uint8_t green,
                                               uint8_t blue) noexcept {
    return static_cast<COLORREF>(red) | (static_cast<COLORREF>(green) << 8u) |
           (static_cast<COLORREF>(blue) << 16u);
}

inline constexpr AnnotationColorPalette kDefaultAnnotationColorPalette = {
    {Make_colorref(0x00, 0x00, 0x00), Make_colorref(0xFF, 0x00, 0x00),
     Make_colorref(0x00, 0xFF, 0x00), Make_colorref(0x00, 0x00, 0xFF),
     Make_colorref(0xFF, 0xFF, 0x00), Make_colorref(0xFF, 0x00, 0xFF),
     Make_colorref(0x00, 0xFF, 0xFF), Make_colorref(0xFF, 0xFF, 0xFF)}};

inline constexpr HighlighterColorPalette kDefaultHighlighterColorPalette = {
    {Make_colorref(0xF7, 0xEB, 0x62), Make_colorref(0x7F, 0xE3, 0x6A),
     Make_colorref(0xFF, 0xB4, 0x4D), Make_colorref(0xFF, 0x79, 0xB9),
     Make_colorref(0x64, 0xC7, 0xFF), Make_colorref(0xC3, 0x8C, 0xFF)}};

inline constexpr int32_t kDefaultAnnotationColorIndex = 0;
inline constexpr int32_t kDefaultHighlighterColorIndex = 0;
inline constexpr int32_t kDefaultHighlighterOpacityPercent = 50;

enum class TextWheelMode : uint8_t { Color, Font };
enum class TextWheelHubSide : uint8_t { Color, Font };

[[nodiscard]] constexpr size_t Text_font_choice_index(TextFontChoice choice) noexcept {
    switch (choice) {
    case TextFontChoice::Sans:
        return 0;
    case TextFontChoice::Serif:
        return 1;
    case TextFontChoice::Mono:
        return 2;
    case TextFontChoice::Art:
        return 3;
    }
    return 0;
}

struct ColorWheelSegmentGeometry final {
    float center_angle_degrees = 0.0f;
    float start_angle_degrees = 0.0f;
    float sweep_angle_degrees = 0.0f;
};

[[nodiscard]] constexpr int32_t Clamp_color_index(int32_t index,
                                                  size_t slot_count) noexcept {
    if (slot_count == 0) {
        return 0;
    }
    return std::clamp(index, 0, static_cast<int32_t>(slot_count) - 1);
}

[[nodiscard]] constexpr int32_t Clamp_annotation_color_index(int32_t index) noexcept {
    return Clamp_color_index(index, kAnnotationColorSlotCount);
}

[[nodiscard]] constexpr int32_t Clamp_highlighter_color_index(int32_t index) noexcept {
    return Clamp_color_index(index, kHighlighterColorSlotCount);
}

[[nodiscard]] ColorWheelSegmentGeometry
Get_color_wheel_segment_geometry(size_t index, size_t segment_count) noexcept;
[[nodiscard]] std::optional<size_t>
Hit_test_color_wheel_segment(PointPx center, PointPx point,
                             size_t segment_count) noexcept;
[[nodiscard]] std::optional<TextWheelHubSide>
Hit_test_text_wheel_hub(PointPx center, PointPx point) noexcept;

} // namespace greenflame::core
