#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {

// Entries GetMouseMovePointsEx keeps in its history buffer.
inline constexpr size_t kMouseMoveHistoryCapacity = 64;

// One mouse-history entry: screen coordinates in physical pixels and the message
// tick time. Coordinates from GetMouseMovePointsEx(GMMP_USE_DISPLAY_POINTS) arrive as
// 16-bit values; Unwrap_mouse_history_coordinate restores the sign.
struct MouseMoveSample final {
    PointPx screen = {};
    uint32_t time_ms = 0;

    constexpr bool operator==(MouseMoveSample const &) const noexcept = default;
};

struct MouseHistoryResult final {
    // Points newer than the last consumed sample, oldest first, in client pixels.
    std::vector<PointPx> points_client = {};
    // The newest sample taken, unwrapped, in screen pixels. Equals last_consumed
    // when the history held nothing newer.
    MouseMoveSample newest = {};
    // True when the buffer was full and its oldest entry is already newer than the
    // last consumed sample: movement in between is lost.
    bool gap = false;
};

// GetMouseMovePointsEx returns display points as 16-bit values: a coordinate left of
// or above the primary monitor comes back as value + 65536.
[[nodiscard]] int32_t Unwrap_mouse_history_coordinate(int32_t value) noexcept;

// Wrap-safe tick comparison (GetMessageTime wraps after 49.7 days).
[[nodiscard]] bool Is_tick_newer(uint32_t time_ms, uint32_t than_ms) noexcept;

// Picks the samples that arrived after last_consumed from a GetMouseMovePointsEx
// buffer (newest first, coordinates as returned). Stops at the exact match of
// last_consumed; without a match keeps only samples strictly newer than its time,
// so movement from before the press is never replayed. Repeated positions are
// dropped. client_origin_screen is the screen position of client pixel (0, 0).
[[nodiscard]] MouseHistoryResult
Collect_new_mouse_points(std::span<const MouseMoveSample> raw_newest_first,
                         MouseMoveSample last_consumed, PointPx client_origin_screen);

} // namespace greenflame::core
