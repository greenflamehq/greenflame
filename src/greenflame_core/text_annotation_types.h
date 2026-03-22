#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {

inline constexpr int32_t kDefaultTextAnnotationPointSize = 12;
inline constexpr size_t kMaxTextFontFamilyChars = 128;
inline constexpr int32_t kMinToolSizeStep = 1;

inline constexpr std::array<std::wstring_view, 4> kDefaultTextFontFamilies = {{
    L"Arial",
    L"Times New Roman",
    L"Courier New",
    L"Comic Sans MS",
}};

enum class TextFontChoice : uint8_t {
    Sans,
    Serif,
    Mono,
    Art,
};

[[nodiscard]] constexpr TextFontChoice
Normalize_text_font_choice(TextFontChoice choice) noexcept {
    switch (choice) {
    case TextFontChoice::Sans:
    case TextFontChoice::Serif:
    case TextFontChoice::Mono:
    case TextFontChoice::Art:
        return choice;
    }
    return TextFontChoice::Sans;
}

// 50-entry table mapping text size step (1-50) to point size.
inline constexpr std::array<int32_t, 50> kTextSizePtTable = {{
    5,  6,  7,  8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,
    22, 23, 24, 26,  28,  31,  34,  36,  40,  43,  47,  51,  55,  60,  65,  71,  77,
    83, 90, 98, 107, 116, 126, 137, 149, 161, 175, 190, 207, 225, 244, 265, 288,
}};
inline constexpr int32_t kMaxToolSizeStep =
    static_cast<int32_t>(kTextSizePtTable.size());

[[nodiscard]] constexpr int32_t Clamp_tool_size_step(int32_t step) noexcept {
    return std::clamp(step, kMinToolSizeStep, kMaxToolSizeStep);
}

[[nodiscard]] constexpr int32_t Text_point_size_from_step(int32_t step) noexcept {
    return kTextSizePtTable[static_cast<size_t>(Clamp_tool_size_step(step) -
                                                kMinToolSizeStep)];
}

struct TextStyleFlags final {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;

    constexpr bool operator==(TextStyleFlags const &) const noexcept = default;
};

struct TextAnnotationBaseStyle final {
    COLORREF color = static_cast<COLORREF>(0x00000000u);
    TextFontChoice font_choice = TextFontChoice::Sans;
    std::wstring font_family = {};
    int32_t point_size = kDefaultTextAnnotationPointSize;

    constexpr bool operator==(TextAnnotationBaseStyle const &) const noexcept = default;
};

struct TextTypingStyle final {
    TextStyleFlags flags = {};

    constexpr bool operator==(TextTypingStyle const &) const noexcept = default;
};

struct TextRun final {
    std::wstring text = {};
    TextStyleFlags flags = {};

    bool operator==(TextRun const &) const noexcept = default;
};

struct TextSelection final {
    int32_t anchor_utf16 = 0;
    int32_t active_utf16 = 0;

    constexpr bool operator==(TextSelection const &) const noexcept = default;
};

struct TextDraftBuffer final {
    TextAnnotationBaseStyle base_style = {};
    std::vector<TextRun> runs = {};
    TextTypingStyle typing_style = {};
    TextSelection selection = {};
    bool overwrite_mode = false;
    int32_t preferred_x_px = 0;

    bool operator==(TextDraftBuffer const &) const noexcept = default;
};

struct TextDraftSnapshot final {
    TextDraftBuffer buffer = {};

    bool operator==(TextDraftSnapshot const &) const noexcept = default;
};

struct TextAnnotation final {
    PointPx origin = {};
    TextAnnotationBaseStyle base_style = {};
    std::vector<TextRun> runs = {};
    RectPx visual_bounds = {};
    int32_t bitmap_width_px = 0;
    int32_t bitmap_height_px = 0;
    int32_t bitmap_row_bytes = 0;
    std::vector<uint8_t> premultiplied_bgra = {};

    bool operator==(TextAnnotation const &) const noexcept = default;
};

} // namespace greenflame::core
