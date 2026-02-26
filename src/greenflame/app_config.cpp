#include "app_config.h"

namespace greenflame {

namespace {

std::filesystem::path Get_config_path() {
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

std::wstring To_wide(std::string const &value) {
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

std::string To_utf8(std::wstring const &value) {
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

std::string_view Trim(std::string_view s) {
    size_t begin = 0;
    while (begin < s.size() &&
           (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r')) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin &&
           (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
        --end;
    }
    return s.substr(begin, end - begin);
}

} // namespace

AppConfig AppConfig::Load() {
    AppConfig config;
    std::filesystem::path const path = Get_config_path();
    if (path.empty() || !std::filesystem::exists(path)) {
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

bool AppConfig::Save() const {
    std::filesystem::path const path = Get_config_path();
    if (path.empty()) {
        return false;
    }
    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        if (!file) {
            return false;
        }

        // UI section: only write non-default values.
        if (!show_balloons) { // default: true
            file << "[ui]\n";
            file << "show_balloons=false\n";
        }

        // Save section: only write non-default values.
        bool wrote_save_header = false;
        auto write_string = [&](char const *key, std::wstring const &value) {
            if (!value.empty()) {
                if (!wrote_save_header) {
                    file << "\n[save]\n";
                    wrote_save_header = true;
                }
                file << key << "=" << To_utf8(value) << "\n";
            }
        };
        write_string("default_save_dir", default_save_dir);
        write_string("last_save_as_dir", last_save_as_dir);
        write_string("filename_pattern_region", filename_pattern_region);
        write_string("filename_pattern_desktop", filename_pattern_desktop);
        write_string("filename_pattern_monitor", filename_pattern_monitor);
        write_string("filename_pattern_window", filename_pattern_window);
        write_string("default_save_format", default_save_format);

        return file.good();
    } catch (...) {
        return false;
    }
}

void AppConfig::Normalize() {
    if (default_save_dir.size() >= MAX_PATH) {
        default_save_dir.resize(MAX_PATH - 1);
    }
    if (last_save_as_dir.size() >= MAX_PATH) {
        last_save_as_dir.resize(MAX_PATH - 1);
    }
    auto clamp_pattern = [](std::wstring &s) {
        if (s.size() > 256) s.resize(256);
    };
    clamp_pattern(filename_pattern_region);
    clamp_pattern(filename_pattern_desktop);
    clamp_pattern(filename_pattern_monitor);
    clamp_pattern(filename_pattern_window);

    if (!default_save_format.empty()) {
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
        } else {
            default_save_format.clear();
        }
    }
}

} // namespace greenflame
