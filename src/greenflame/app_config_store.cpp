#include "app_config_store.h"
#include "greenflame_core/app_config_json.h"

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
    path /= L"greenflame.json";
    return path;
}

[[nodiscard]] std::optional<std::string>
Read_text_file(std::filesystem::path const &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

[[nodiscard]] std::wstring
Build_config_issue_detail(core::AppConfigDiagnostic const &diagnostic) {
    std::wstring detail = {};
    if (!diagnostic.message.empty()) {
        if (diagnostic.line.has_value() && diagnostic.column.has_value()) {
            detail += L"Line ";
            detail += std::to_wstring(*diagnostic.line);
            detail += L", column ";
            detail += std::to_wstring(*diagnostic.column);
            detail += L": ";
        }
        detail += diagnostic.message;
    }
    return detail;
}

[[nodiscard]] AppConfigLoadIssue
Build_config_issue(core::AppConfigDiagnostic const &diagnostic) {
    return AppConfigLoadIssue{
        L"The config file has an error.", Build_config_issue_detail(diagnostic),
        L"Using defaults for anything that could not be loaded. The file will not "
        L"be saved until it is fixed, and transient changes may be lost after a "
        L"valid reload."};
}

[[nodiscard]] bool Can_overwrite_config_file(std::filesystem::path const &path) {
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::optional<std::string> const existing_text = Read_text_file(path);
    if (!existing_text.has_value()) {
        return false;
    }

    return !core::Parse_app_config_json_with_diagnostics(*existing_text).Has_error();
}

} // namespace

std::filesystem::path Get_app_config_dir() {
    std::filesystem::path const path = Get_config_path();
    if (path.empty()) {
        return {};
    }
    return path.parent_path();
}

std::filesystem::path Get_config_file_path() { return Get_config_path(); }

AppConfigLoadResult Load_app_config() {
    AppConfigLoadResult result = {};
    result.path = Get_config_path();
    if (result.path.empty()) {
        result.issue =
            AppConfigLoadIssue{L"The config file path could not be resolved.",
                               {},
                               L"Using defaults for this session."};
        return result;
    }
    if (!std::filesystem::exists(result.path)) {
        (void)Save_app_config(result.config); // creates dir + empty file on first run
        return result;
    }

    try {
        std::optional<std::string> const json_text = Read_text_file(result.path);
        if (!json_text.has_value()) {
            result.issue = AppConfigLoadIssue{
                L"The config file could not be read.",
                {},
                L"Using defaults for this session. The file will not be saved until "
                L"it can be read again."};
            return result;
        }

        core::AppConfigParseResult const parsed =
            core::Parse_app_config_json_with_diagnostics(*json_text);
        result.config = parsed.config;
        if (parsed.diagnostic.has_value()) {
            result.issue = Build_config_issue(*parsed.diagnostic);
        }
    } catch (...) {
        result.issue = AppConfigLoadIssue{
            L"The config file could not be loaded.",
            {},
            L"Using defaults for this session. The file will not be saved until it "
            L"is fixed."};
    }
    return result;
}

bool Save_app_config(core::AppConfig const &config) {
    std::filesystem::path const path = Get_config_path();
    if (path.empty()) {
        return false;
    }
    try {
        if (!Can_overwrite_config_file(path)) {
            return false;
        }

        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        if (!file) {
            return false;
        }
        std::string const json_str = core::Serialize_app_config_json(config);
        file << json_str;
        return file.good();
    } catch (...) {
        return false;
    }
}

} // namespace greenflame
