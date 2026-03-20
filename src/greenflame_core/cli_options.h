#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {

enum class CliCaptureMode : uint8_t {
    None = 0,
    Region = 1,
    Window = 2,
    Monitor = 3,
    Desktop = 4,
};

enum class CliOutputFormat : uint8_t {
    Png = 0,
    Jpeg = 1,
    Bmp = 2,
};

enum class CliAction : uint8_t {
    None = 0,
    Help = 1,
    Version = 2,
};

[[nodiscard]] constexpr bool Is_capture_mode(CliCaptureMode mode) noexcept {
    return mode == CliCaptureMode::Region || mode == CliCaptureMode::Window ||
           mode == CliCaptureMode::Monitor || mode == CliCaptureMode::Desktop;
}

struct CliOptions final {
    std::wstring window_name = {};
    std::wstring output_path = {};
    std::optional<RectPx> region_px = std::nullopt;
    std::optional<InsetsPx> padding_px = std::nullopt;
    std::optional<COLORREF> padding_color_override = std::nullopt;
    std::optional<std::uintptr_t> window_hwnd = std::nullopt;
    int32_t monitor_id = 0; // 1-based.
    std::optional<CliOutputFormat> output_format = std::nullopt;
    CliAction action = CliAction::None;
    CliCaptureMode capture_mode = CliCaptureMode::None;
    bool overwrite_output = false;
#ifdef DEBUG
    bool testing_1_2 = false;
#endif
};

struct CliParseResult final {
    std::wstring error_message = {};
    CliOptions options = {};
    bool ok = false;
};

[[nodiscard]] CliParseResult Parse_cli_arguments(std::vector<std::wstring> const &args,
                                                 bool debug_build);

[[nodiscard]] std::wstring Build_cli_help_text(bool debug_build);

} // namespace greenflame::core
