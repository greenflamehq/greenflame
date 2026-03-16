#include "greenflame_core/app_config.h"
#include "greenflame_core/annotation_types.h"

namespace greenflame::core {

namespace {

constexpr size_t kMaxWindowsPathChars = 260;
constexpr size_t kMaxConfigPathChars = kMaxWindowsPathChars - 1;
constexpr int32_t kMinBrushWidthPx = 1;
constexpr int32_t kMaxBrushWidthPx = 50;
constexpr size_t kMaxTextFontFamilyChars = 128;
constexpr std::array<int32_t, 24> kAllowedTextPointSizes = {{
    5,  8,  9,  10, 11, 12, 14, 16,  18,  20,  22,  24,
    26, 28, 36, 48, 72, 84, 96, 108, 144, 192, 216, 288,
}};

[[nodiscard]] int32_t Normalize_text_point_size(int32_t point_size) noexcept {
    int32_t nearest = kAllowedTextPointSizes.front();
    int64_t nearest_distance =
        std::abs(static_cast<int64_t>(point_size) - static_cast<int64_t>(nearest));
    for (int32_t const allowed_size : kAllowedTextPointSizes) {
        int64_t const distance = std::abs(static_cast<int64_t>(point_size) -
                                          static_cast<int64_t>(allowed_size));
        if (distance < nearest_distance) {
            nearest = allowed_size;
            nearest_distance = distance;
        }
    }
    return nearest;
}

[[nodiscard]] TextFontChoice
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

[[nodiscard]] std::wstring_view
Default_text_font_family(TextFontChoice choice) noexcept {
    switch (choice) {
    case TextFontChoice::Sans:
        return L"Arial";
    case TextFontChoice::Serif:
        return L"Times New Roman";
    case TextFontChoice::Mono:
        return L"Courier New";
    case TextFontChoice::Art:
        return L"Comic Sans MS";
    }
    return L"Arial";
}

void Normalize_text_font_family(std::wstring &value, TextFontChoice choice) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::iswspace(value[begin]) != 0) {
        ++begin;
    }
    while (end > begin && std::iswspace(value[end - 1]) != 0) {
        --end;
    }

    value = value.substr(begin, end - begin);
    if (value.size() > kMaxTextFontFamilyChars) {
        value.resize(kMaxTextFontFamilyChars);
    }
    if (value.empty()) {
        value.assign(Default_text_font_family(choice));
    }
}

} // namespace

void AppConfig::Normalize() {
    if (default_save_dir.size() > kMaxConfigPathChars) {
        default_save_dir.resize(kMaxConfigPathChars);
    }
    if (last_save_as_dir.size() > kMaxConfigPathChars) {
        last_save_as_dir.resize(kMaxConfigPathChars);
    }

    auto clamp_pattern = [](std::wstring &value) {
        if (value.size() > 256) {
            value.resize(256);
        }
    };
    clamp_pattern(filename_pattern_region);
    clamp_pattern(filename_pattern_desktop);
    clamp_pattern(filename_pattern_monitor);
    clamp_pattern(filename_pattern_window);
    brush_width_px = std::clamp(brush_width_px, kMinBrushWidthPx, kMaxBrushWidthPx);
    current_annotation_color_index =
        Clamp_annotation_color_index(current_annotation_color_index);
    current_highlighter_color_index =
        Clamp_highlighter_color_index(current_highlighter_color_index);
    highlighter_opacity_percent =
        std::clamp(highlighter_opacity_percent, StrokeStyle::kMinOpacityPercent,
                   StrokeStyle::kMaxOpacityPercent);
    text_size_points = Normalize_text_point_size(text_size_points);
    text_current_font = Normalize_text_font_choice(text_current_font);
    bubble_current_font = Normalize_text_font_choice(bubble_current_font);
    Normalize_text_font_family(text_font_sans, TextFontChoice::Sans);
    Normalize_text_font_family(text_font_serif, TextFontChoice::Serif);
    Normalize_text_font_family(text_font_mono, TextFontChoice::Mono);
    Normalize_text_font_family(text_font_art, TextFontChoice::Art);
    tool_size_overlay_duration_ms = std::max(tool_size_overlay_duration_ms, 0);

    if (default_save_format.empty()) {
        return;
    }

    std::wstring normalized;
    normalized.reserve(default_save_format.size());
    for (wchar_t const ch : default_save_format) {
        normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }

    size_t begin = 0;
    size_t end = normalized.size();
    while (begin < end && std::iswspace(normalized[begin]) != 0) {
        ++begin;
    }
    while (end > begin && std::iswspace(normalized[end - 1]) != 0) {
        --end;
    }
    normalized = normalized.substr(begin, end - begin);

    if (normalized == L"jpeg") {
        normalized = L"jpg";
    }
    if (normalized == L"png" || normalized == L"jpg" || normalized == L"bmp") {
        default_save_format = normalized;
        return;
    }
    default_save_format.clear();
}

} // namespace greenflame::core
