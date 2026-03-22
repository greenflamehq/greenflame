#pragma once

#include "greenflame_core/annotation_types.h"
#include "greenflame_core/app_config.h"

namespace greenflame::core {

enum class CliAnnotationInputKind : uint8_t {
    InlineJson = 0,
    FilePath = 1,
};

[[nodiscard]] CliAnnotationInputKind
Classify_cli_annotation_input(std::wstring_view value) noexcept;

[[nodiscard]] std::array<std::wstring, 4>
Resolve_text_font_families(AppConfig const &config);

struct CliAnnotationParseContext final {
    RectPx capture_rect_screen = {};
    RectPx virtual_desktop_bounds = {};
    AppConfig const *config = nullptr;

    constexpr bool
    operator==(CliAnnotationParseContext const &) const noexcept = default;
};

struct CliAnnotationParseResult final {
    std::wstring error_message = {};
    std::vector<Annotation> annotations = {};
    bool ok = false;

    bool operator==(CliAnnotationParseResult const &) const noexcept = default;
};

[[nodiscard]] CliAnnotationParseResult
Parse_cli_annotations_json(std::string_view json_text,
                           CliAnnotationParseContext const &context) noexcept;

} // namespace greenflame::core
