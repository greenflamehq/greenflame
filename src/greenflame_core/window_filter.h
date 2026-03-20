#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {

struct WindowCandidateInfo final {
    std::wstring title = {};
    std::wstring class_name = {};
    RectPx rect = {};
    std::uintptr_t hwnd_value = 0;
};

[[nodiscard]] bool Is_terminal_window_class(std::wstring_view class_name) noexcept;

[[nodiscard]] bool Is_cli_invocation_window(WindowCandidateInfo const &candidate,
                                            std::wstring_view query) noexcept;

[[nodiscard]] std::vector<WindowCandidateInfo>
Filter_cli_invocation_window(std::vector<WindowCandidateInfo> matches,
                             std::wstring_view query);

[[nodiscard]] std::wstring
Format_window_candidate_line(WindowCandidateInfo const &candidate, size_t index);

} // namespace greenflame::core
