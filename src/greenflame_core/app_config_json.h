#pragma once

#include "greenflame_core/app_config.h"

namespace greenflame::core {

enum class AppConfigDiagnosticKind {
    Parse,
    Schema,
};

struct AppConfigDiagnostic final {
    AppConfigDiagnosticKind kind = AppConfigDiagnosticKind::Schema;
    std::wstring message = {};
    std::optional<size_t> line = std::nullopt;
    std::optional<size_t> column = std::nullopt;
};

struct AppConfigParseResult final {
    AppConfig config = {};
    std::optional<AppConfigDiagnostic> diagnostic = std::nullopt;

    [[nodiscard]] bool Has_error() const noexcept { return diagnostic.has_value(); }
};

[[nodiscard]] AppConfigParseResult
Parse_app_config_json_with_diagnostics(std::string_view json_text) noexcept;

[[nodiscard]] std::optional<AppConfig>
Parse_app_config_json(std::string_view json_text) noexcept;

[[nodiscard]] std::string Serialize_app_config_json(AppConfig const &config);

} // namespace greenflame::core
