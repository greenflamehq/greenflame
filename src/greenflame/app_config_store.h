#pragma once

#include "greenflame_core/app_config.h"

namespace greenflame {

struct AppConfigLoadIssue final {
    std::wstring summary = {};
    std::wstring detail = {};
    std::wstring consequences = {};
};

struct AppConfigLoadResult final {
    core::AppConfig config = {};
    std::filesystem::path path = {};
    std::optional<AppConfigLoadIssue> issue = std::nullopt;
};

[[nodiscard]] std::filesystem::path Get_app_config_dir();
[[nodiscard]] std::filesystem::path Get_config_file_path();
[[nodiscard]] AppConfigLoadResult Load_app_config();
[[nodiscard]] bool Save_app_config(core::AppConfig const &config);

} // namespace greenflame
