#include "greenflame_core/app_config.h"

namespace greenflame::core {

namespace {

constexpr size_t kMaxWindowsPathChars = 260;
constexpr size_t kMaxConfigPathChars = kMaxWindowsPathChars - 1;
constexpr int32_t kMinBrushWidthPx = 1;
constexpr int32_t kMaxBrushWidthPx = 50;

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
    brush_width_px =
        std::clamp(brush_width_px, kMinBrushWidthPx, kMaxBrushWidthPx);
    tool_size_overlay_duration_ms =
        std::max(tool_size_overlay_duration_ms, 0);

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
