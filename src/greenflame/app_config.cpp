#include "app_config.h"

#include <easyjson.h>

namespace greenflame {

namespace {

std::filesystem::path Get_config_path() {
    wchar_t app_data[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, app_data))) {
        return {};
    }
    std::filesystem::path path(app_data);
    path /= L"greenflame";
    path /= L"greenflame.json";
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

} // namespace

AppConfig AppConfig::Load() {
    AppConfig config;
    std::filesystem::path const path = Get_config_path();
    if (path.empty() || !std::filesystem::exists(path)) {
        return config;
    }
    try {
        easyjson::JSON root = easyjson::JSON::load_file(path.string());
        if (root.has_key("ui")) {
            easyjson::JSON const &ui = root["ui"];
            if (ui.has_key("show_balloons")) {
                easyjson::JSON const &value = ui["show_balloons"];
                if (value.JSON_type() == easyjson::JSON::Class::Boolean) {
                    config.show_balloons = value.to_bool();
                }
            }
        }
        if (root.has_key("save")) {
            easyjson::JSON const &save = root["save"];
            auto read_string = [&](char const *key, std::wstring &target) {
                if (save.has_key(key)) {
                    easyjson::JSON const &value = save[key];
                    if (value.JSON_type() == easyjson::JSON::Class::String &&
                        value.Internal.String.has_value()) {
                        target = To_wide(*value.Internal.String.value());
                    }
                }
            };
            std::wstring legacy_last_save_dir = {};
            read_string("default_save_dir", config.default_save_dir);
            read_string("last_save_as_dir", config.last_save_as_dir);
            read_string("last_save_dir", legacy_last_save_dir);
            if (config.default_save_dir.empty() && !legacy_last_save_dir.empty()) {
                config.default_save_dir = legacy_last_save_dir;
            }
            if (config.last_save_as_dir.empty() && !legacy_last_save_dir.empty()) {
                config.last_save_as_dir = legacy_last_save_dir;
            }
            read_string("filename_pattern_region", config.filename_pattern_region);
            read_string("filename_pattern_desktop", config.filename_pattern_desktop);
            read_string("filename_pattern_monitor", config.filename_pattern_monitor);
            read_string("filename_pattern_window", config.filename_pattern_window);
            read_string("default_save_format", config.default_save_format);
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
        easyjson::JSON root = easyjson::object();

        // UI section: only write non-default values.
        if (!show_balloons) { // default: true
            root["ui"]["show_balloons"] = show_balloons;
        }

        // Save section: only write non-default values.
        if (!default_save_dir.empty()) {
            root["save"]["default_save_dir"] = To_utf8(default_save_dir);
        }
        if (!last_save_as_dir.empty()) {
            root["save"]["last_save_as_dir"] = To_utf8(last_save_as_dir);
        }
        auto write_if_set = [&](char const *key, std::wstring const &value) {
            if (!value.empty()) {
                root["save"][key] = To_utf8(value);
            }
        };
        write_if_set("filename_pattern_region", filename_pattern_region);
        write_if_set("filename_pattern_desktop", filename_pattern_desktop);
        write_if_set("filename_pattern_monitor", filename_pattern_monitor);
        write_if_set("filename_pattern_window", filename_pattern_window);
        write_if_set("default_save_format", default_save_format);

        std::ofstream file(path);
        if (!file) {
            return false;
        }
        file << root.dump();
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
}

} // namespace greenflame
