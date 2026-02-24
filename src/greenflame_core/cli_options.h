#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {

enum class CliCaptureMode : uint8_t {
    None = 0,
    Region = 1,
    Window = 2,
    Monitor = 3,
    Desktop = 4,
    Help = 5,
};

[[nodiscard]] constexpr bool Is_capture_mode(CliCaptureMode mode) noexcept {
    return mode == CliCaptureMode::Region || mode == CliCaptureMode::Window ||
           mode == CliCaptureMode::Monitor || mode == CliCaptureMode::Desktop;
}

struct CliOptions final {
    CliCaptureMode capture_mode = CliCaptureMode::None;
    std::optional<RectPx> region_px = std::nullopt;
    std::wstring window_name = {};
    int32_t monitor_id = 0; // 1-based.
    std::wstring output_path = {};
#ifdef DEBUG
    bool testing_1_2 = false;
#endif
};

struct CliParseResult final {
    bool ok = false;
    CliOptions options = {};
    std::wstring error_message = {};
};

[[nodiscard]] CliParseResult Parse_cli_arguments(std::vector<std::wstring> const &args,
                                                 bool debug_build);

[[nodiscard]] std::wstring Build_cli_help_text(bool debug_build);

} // namespace greenflame::core
