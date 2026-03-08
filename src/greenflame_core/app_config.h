#pragma once

namespace greenflame::core {

struct AppConfig final {
    std::wstring default_save_dir = {};
    std::wstring last_save_as_dir = {};
    std::wstring filename_pattern_region = {};
    std::wstring filename_pattern_desktop = {};
    std::wstring filename_pattern_monitor = {};
    std::wstring filename_pattern_window = {};
    std::wstring default_save_format = {}; // "png" (default), "jpg"/"jpeg", or "bmp".
    int32_t brush_width_px = 2;
    int32_t tool_size_overlay_duration_ms = 800;
    bool show_balloons = true;
    bool show_selection_size_side_labels = true;
    bool show_selection_size_center_label = true;

    void Normalize();
};

} // namespace greenflame::core
