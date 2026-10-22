#pragma once

#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"

namespace greenflame::core {

// One overlay window: where it sits on screen, and the overlay (client) pixels it
// shows. Client pixel (0, 0) is the virtual desktop's top-left.
struct OverlaySurfacePlan final {
    RectPx rect_screen = {};
    RectPx rect_client = {};

    constexpr bool operator==(OverlaySurfacePlan const &) const noexcept = default;
};

// The overlay's windows for one session: one per monitor, or one over the whole
// virtual desktop (span). primary is the index of the window on the cursor's monitor:
// it owns keyboard focus, timers, the clipboard and dialogs.
struct OverlayLayout final {
    RectPx desktop_screen = {};
    std::vector<OverlaySurfacePlan> surfaces = {};
    size_t primary = 0;
    // True for one window per monitor, false for one window over the desktop.
    bool per_monitor = false;
};

// Plans the overlay's windows. per_monitor: one window per monitor, else one window
// over desktop_screen. No monitors (or no monitor inside the desktop) falls back to
// span. The primary is the window containing cursor_screen, else the nearest one.
[[nodiscard]] OverlayLayout
Plan_overlay_layout(std::span<const MonitorWithBounds> monitors, RectPx desktop_screen,
                    PointPx cursor_screen, bool per_monitor);

// Index of the rect containing point, else of the rect nearest to it (by squared
// distance to its edges; the first one on a tie). 0 for no rects.
[[nodiscard]] size_t Index_of_rect_nearest(std::span<const RectPx> rects,
                                           PointPx point) noexcept;

// True when a created window sits exactly where planned: its window rect equals
// planned_screen and its client area has the same size (no frame, no DPI scaling).
[[nodiscard]] bool Surface_matches_plan(RectPx planned_screen,
                                        RectPx window_rect_screen,
                                        SizePx client_size) noexcept;

// True when the monitors and the virtual desktop still match the layout: the same
// desktop and, for a per-monitor layout, the same set of monitor rects in any order.
// A display change that leaves this true needs no action.
[[nodiscard]] bool Layout_still_matches(OverlayLayout const &layout,
                                        std::span<const MonitorWithBounds> monitors,
                                        RectPx desktop_screen);

} // namespace greenflame::core
