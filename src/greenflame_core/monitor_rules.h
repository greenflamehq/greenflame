#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {
constexpr int32_t kDefaultMonitorScalePercent = 100;
constexpr int32_t kMinMonitorScalePercent = 50;
constexpr int32_t kMaxMonitorScalePercent = 500;

enum class MonitorOrientation : uint8_t {
    Landscape = 0,
    Portrait = 1,
};

struct MonitorDpiScale final {
    // Windows "scale" is typically 100, 125, 150, 175, 200...
    // We store it as an integer percent to avoid floats.
    int32_t percent{kDefaultMonitorScalePercent};

    [[nodiscard]] constexpr bool Is_valid() const noexcept {
        return percent >= kMinMonitorScalePercent && percent <= kMaxMonitorScalePercent;
    }
    [[nodiscard]] constexpr bool operator==(const MonitorDpiScale &o) const noexcept {
        return percent == o.percent;
    }
};

struct MonitorInfo final {
    // Pure, testable metadata (no Win32 types in core).
    MonitorDpiScale dpi_scale{};
    MonitorOrientation orientation{MonitorOrientation::Landscape};
};

// Monitor entry with bounds in physical pixels. Core only consumes this; Win32
// fills it.
struct MonitorWithBounds final {
    RectPx bounds{};
    MonitorInfo info{};
};

enum class CrossMonitorSelectionDecision : uint8_t {
    Allowed = 0,

    // Refuse when selection spans monitors with different DPI scales.
    RefusedDifferentDpiScale = 1,

    // Refuse when selection spans monitors with different orientation.
    RefusedDifferentOrientation = 2,

    // Refuse if inputs are malformed (e.g., empty span).
    RefusedInvalidInput = 3,
};

// Implements Greenflame's rule:
// Cross-monitor selection is allowed only if ALL touched monitors share:
// - identical DPI scale factor
// - identical orientation
//
// Resolution differences are allowed and intentionally ignored here.
[[nodiscard]] CrossMonitorSelectionDecision
Decide_cross_monitor_selection(std::span<const MonitorInfo> touched_monitors) noexcept;

[[nodiscard]] constexpr bool Is_allowed(CrossMonitorSelectionDecision d) noexcept {
    return d == CrossMonitorSelectionDecision::Allowed;
}

// --- DPI scale (pure; no Win32) ---
// Converts raw DPI to scale percent: 96 -> 100, 120 -> 125, 144 -> 150, etc.
[[nodiscard]] int32_t Dpi_to_scale_percent(int dpi) noexcept;

// --- Monitor model helpers (pure; no Win32) ---

// Returns index of the monitor whose bounds contain p, or nullopt if none.
[[nodiscard]] std::optional<size_t>
Index_of_monitor_containing(PointPx p,
                            std::span<const MonitorWithBounds> monitors) noexcept;

// Returns indices of monitors whose bounds intersect r (non-empty
// intersection).
[[nodiscard]] std::vector<size_t>
Indices_of_monitors_intersecting(RectPx r,
                                 std::span<const MonitorWithBounds> monitors) noexcept;

// Monitor bounds moved to client space, where client pixel (0, 0) is the screen
// pixel client_origin_screen. One rect per monitor, in the same order.
[[nodiscard]] std::vector<RectPx>
Monitor_rects_in_client(std::span<const MonitorWithBounds> monitors,
                        PointPx client_origin_screen);

// --- Selection policy ---
// Returns the allowed selection rect: candidate unchanged if cross-monitor is
// allowed, otherwise candidate clamped to the starting monitor. Empty candidate
// returned as-is. Start outside all monitors: clamped to first monitor
// intersecting the candidate.
[[nodiscard]] RectPx
Allowed_selection_rect(RectPx candidate, PointPx start,
                       std::span<const MonitorWithBounds> monitors) noexcept;
} // namespace greenflame::core
