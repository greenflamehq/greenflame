#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {

// Eight contour handles: four corners and four edge midpoints.
// Order allows testing corners before edges for hit-test priority.
enum class SelectionHandle : uint8_t {
    TopLeft = 0,
    TopRight = 1,
    BottomRight = 2,
    BottomLeft = 3,
    Top = 4,
    Right = 5,
    Bottom = 6,
    Left = 7,
};

// Corner zone size used by both hit-testing and border highlight rendering.
inline constexpr int kMaxCornerSizePx = 16;

// Hit-test against the border band. Returns the zone under the cursor,
// or nullopt if not within kBorderHitBandPx px of any border edge.
// Corner size = min(kMaxCornerSizePx, side/2) per axis.
[[nodiscard]] std::optional<SelectionHandle>
Hit_test_border_zone(RectPx selection, PointPx cursor_client_px) noexcept;

// Resize rect: anchor rect with the given handle moved to cursor position.
// Opposite corner/edge stays fixed. Result is normalized and has minimum size 1x1.
[[nodiscard]] RectPx Resize_rect_from_handle(RectPx anchor, SelectionHandle handle,
                                             PointPx cursor_px) noexcept;

// Anchor point for Allowed_selection_rect when resizing: the fixed corner when
// dragging a corner, or the center of the fixed edge when dragging an edge.
[[nodiscard]] PointPx Anchor_point_for_resize_policy(RectPx rect,
                                                     SelectionHandle handle) noexcept;

} // namespace greenflame::core
