#pragma once

#include "greenflame_core/rect_px.h"

#include <optional>
#include <vector>

namespace greenflame {

enum class WindowObscuration : uint8_t {
    None = 0,
    Partial = 1,
    Full = 2,
};

[[nodiscard]] std::optional<HWND> Get_window_under_cursor(POINT screen_pt,
                                                          HWND exclude_hwnd);
[[nodiscard]] std::optional<greenflame::core::RectPx> Get_window_rect(HWND hwnd);
[[nodiscard]] std::optional<greenflame::core::RectPx>
Get_foreground_window_rect(HWND exclude_hwnd);
[[nodiscard]] std::optional<greenflame::core::RectPx>
Get_window_rect_under_cursor(POINT screen_pt, HWND exclude_hwnd);
void Get_visible_top_level_window_rects(HWND exclude_hwnd,
                                        std::vector<greenflame::core::RectPx> &out);
[[nodiscard]] WindowObscuration Get_window_obscuration(HWND hwnd);

} // namespace greenflame
