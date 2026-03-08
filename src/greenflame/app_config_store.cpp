#include "app_config_store.h"

namespace greenflame {

namespace {

[[nodiscard]] std::filesystem::path Get_config_path() {
    wchar_t home[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, home))) {
        return {};
    }
    std::filesystem::path path(home);
    path /= L".config";
    path /= L"greenflame";
    path /= L"greenflame.ini";
    return path;
}

[[nodiscard]] std::wstring To_wide(std::string const &value) {
    if (value.empty()) {
        return {};
    }
    int const required_chars =
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required_chars <= 1) {
        return {};
    }
    std::wstring out(static_cast<size_t>(required_chars - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), required_chars);
    return out;
}

[[nodiscard]] std::string To_utf8(std::wstring const &value) {
    if (value.empty()) {
        return {};
    }
    int const required_chars = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
                                                   nullptr, 0, nullptr, nullptr);
    if (required_chars <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(required_chars - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), required_chars,
                        nullptr, nullptr);
    return out;
}

[[nodiscard]] std::string_view Trim(std::string_view value) {
    size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r')) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                           value[end - 1] == '\r')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

[[nodiscard]] bool Try_parse_int32(std::string_view value, int32_t &out) {
    std::string_view const trimmed = Trim(value);
    if (trimmed.empty()) {
        return false;
    }

    bool negative = false;
    size_t pos = 0;
    if (trimmed[0] == '+' || trimmed[0] == '-') {
        negative = trimmed[0] == '-';
        pos = 1;
    }
    if (pos >= trimmed.size()) {
        return false;
    }

    int64_t parsed = 0;
    for (; pos < trimmed.size(); ++pos) {
        char const ch = trimmed[pos];
        if (ch < '0' || ch > '9') {
            return false;
        }
        parsed = parsed * 10 + static_cast<int64_t>(ch - '0');
        if (!negative && parsed > 2147483647LL) {
            out = 2147483647;
            return true;
        }
        if (negative && parsed > 2147483648LL) {
            out = -2147483647 - 1;
            return true;
        }
    }

    if (negative) {
        parsed = -parsed;
    }
    out = static_cast<int32_t>(parsed);
    return true;
}

} // namespace

std::filesystem::path Get_app_config_dir() {
    std::filesystem::path const path = Get_config_path();
    if (path.empty()) {
        return {};
    }
    return path.parent_path();
}

core::AppConfig Load_app_config() {
    core::AppConfig config;
    std::filesystem::path const path = Get_config_path();
    if (path.empty() || !std::filesystem::exists(path)) {
        (void)Save_app_config(config); // creates dir + empty file on first run
        return config;
    }
    try {
        std::ifstream file(path);
        if (!file) {
            return config;
        }
        std::string section;
        std::string line;
        while (std::getline(file, line)) {
            std::string_view sv = Trim(line);
            if (sv.empty() || sv[0] == ';' || sv[0] == '#') {
                continue;
            }
            if (sv[0] == '[') {
                size_t const close = sv.find(']');
                if (close != std::string_view::npos) {
                    section = std::string(sv.substr(1, close - 1));
                }
                continue;
            }
            size_t const eq = sv.find('=');
            if (eq == std::string_view::npos) {
                continue;
            }
            std::string const key{Trim(sv.substr(0, eq))};
            std::string const value{Trim(sv.substr(eq + 1))};

            if (section == "ui") {
                if (key == "show_balloons") {
                    config.show_balloons = (value == "true" || value == "1");
                } else if (key == "show_selection_size_side_labels") {
                    config.show_selection_size_side_labels =
                        (value == "true" || value == "1");
                } else if (key == "show_selection_size_center_label") {
                    config.show_selection_size_center_label =
                        (value == "true" || value == "1");
                } else if (key == "tool_size_overlay_duration_ms") {
                    int32_t parsed = 0;
                    if (Try_parse_int32(value, parsed)) {
                        config.tool_size_overlay_duration_ms = parsed;
                    }
                }
            } else if (section == "tools") {
                if (key == "brush_width") {
                    int32_t parsed = 0;
                    if (Try_parse_int32(value, parsed)) {
                        config.brush_width_px = parsed;
                    }
                }
            } else if (section == "save") {
                auto read = [&](char const *name, std::wstring &target) {
                    if (key == name) {
                        target = To_wide(value);
                    }
                };
                read("default_save_dir", config.default_save_dir);
                read("last_save_as_dir", config.last_save_as_dir);
                read("filename_pattern_region", config.filename_pattern_region);
                read("filename_pattern_desktop", config.filename_pattern_desktop);
                read("filename_pattern_monitor", config.filename_pattern_monitor);
                read("filename_pattern_window", config.filename_pattern_window);
                read("default_save_format", config.default_save_format);
            }
        }
    } catch (...) {
        // Parse error or file read error: return default config.
    }
    config.Normalize();
    return config;
}

bool Save_app_config(core::AppConfig const &config) {
    std::filesystem::path const path = Get_config_path();
    if (path.empty()) {
        return false;
    }
    core::AppConfig const defaults{};
    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        if (!file) {
            return false;
        }

        // UI section: only write non-default values.
        bool wrote_ui_header = false;
        auto write_ui_bool = [&](char const *key, bool value) {
            if (value) {
                return;
            }
            if (!wrote_ui_header) {
                file << "[ui]\n";
                wrote_ui_header = true;
            }
            file << key << "=false\n";
        };
        write_ui_bool("show_balloons", config.show_balloons);
        write_ui_bool("show_selection_size_side_labels",
                      config.show_selection_size_side_labels);
        write_ui_bool("show_selection_size_center_label",
                      config.show_selection_size_center_label);
        if (config.tool_size_overlay_duration_ms != defaults.tool_size_overlay_duration_ms) {
            if (!wrote_ui_header) {
                file << "[ui]\n";
                wrote_ui_header = true;
            }
            file << "tool_size_overlay_duration_ms="
                 << config.tool_size_overlay_duration_ms << "\n";
        }

        bool wrote_tools_header = false;
        if (config.brush_width_px != defaults.brush_width_px) {
            file << (wrote_ui_header ? "\n" : "") << "[tools]\n";
            wrote_tools_header = true;
            file << "brush_width=" << config.brush_width_px << "\n";
        }

        // Save section: only write non-default values.
        bool wrote_save_header = false;
        auto write_string = [&](char const *key, std::wstring const &value) {
            if (!value.empty()) {
                if (!wrote_save_header) {
                    file << ((wrote_ui_header || wrote_tools_header) ? "\n" : "")
                         << "[save]\n";
                    wrote_save_header = true;
                }
                file << key << "=" << To_utf8(value) << "\n";
            }
        };
        write_string("default_save_dir", config.default_save_dir);
        write_string("last_save_as_dir", config.last_save_as_dir);
        write_string("filename_pattern_region", config.filename_pattern_region);
        write_string("filename_pattern_desktop", config.filename_pattern_desktop);
        write_string("filename_pattern_monitor", config.filename_pattern_monitor);
        write_string("filename_pattern_window", config.filename_pattern_window);
        write_string("default_save_format", config.default_save_format);

        return file.good();
    } catch (...) {
        return false;
    }
}

} // namespace greenflame
