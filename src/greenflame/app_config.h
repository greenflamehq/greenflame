#pragma once

namespace greenflame {

class AppConfig final {
  public:
    bool show_balloons = true;
    std::wstring default_save_dir = {};
    std::wstring last_save_as_dir = {};
    std::wstring filename_pattern_region = {};
    std::wstring filename_pattern_desktop = {};
    std::wstring filename_pattern_monitor = {};
    std::wstring filename_pattern_window = {};
    std::wstring default_save_format = {}; // "png" (default), "jpg", or "bmp".

    [[nodiscard]] static AppConfig Load();
    [[nodiscard]] bool Save() const;
    void Normalize();
};

} // namespace greenflame
