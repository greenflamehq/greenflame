#include "selection_handles.h"

namespace greenflame::core {

namespace {

constexpr int32_t kMinSize = 1;
constexpr int kBorderHitBandPx = 5;

} // namespace

std::optional<SelectionHandle> Hit_test_border_zone(RectPx selection,
                                                    PointPx cursor_client_px) noexcept {
    RectPx const r = selection.Normalized();
    if (r.Is_empty()) return std::nullopt;

    int const band = kBorderHitBandPx;
    int const w = r.Width();
    int const h = r.Height();
    int const corner_w = std::min(kMaxCornerSizePx, w / 2);
    int const corner_h = std::min(kMaxCornerSizePx, h / 2);

    int const cx = cursor_client_px.x;
    int const cy = cursor_client_px.y;

    // Is cursor within band of each edge (AND within that edge's lateral extent)?
    bool const on_top    = (cy >= r.top - band && cy <= r.top + band) &&
                           cx >= r.left - band && cx <= r.right - 1 + band;
    bool const on_bottom = (cy >= r.bottom - 1 - band && cy <= r.bottom - 1 + band) &&
                           cx >= r.left - band && cx <= r.right - 1 + band;
    bool const on_left   = (cx >= r.left - band && cx <= r.left + band) &&
                           cy >= r.top - band && cy <= r.bottom - 1 + band;
    bool const on_right  = (cx >= r.right - 1 - band && cx <= r.right - 1 + band) &&
                           cy >= r.top - band && cy <= r.bottom - 1 + band;

    if (!on_top && !on_bottom && !on_left && !on_right) return std::nullopt;

    // Corner zone membership per axis.
    bool const in_lc = cx <  r.left  + corner_w;
    bool const in_rc = cx >= r.right - corner_w;
    bool const in_tc = cy <  r.top   + corner_h;
    bool const in_bc = cy >= r.bottom - corner_h;

    // Corners first (priority over edges).
    if ((on_top && in_lc) || (on_left && in_tc))    return SelectionHandle::TopLeft;
    if ((on_top && in_rc) || (on_right && in_tc))   return SelectionHandle::TopRight;
    if ((on_bottom && in_rc) || (on_right && in_bc)) return SelectionHandle::BottomRight;
    if ((on_bottom && in_lc) || (on_left && in_bc)) return SelectionHandle::BottomLeft;

    // Edges (on band but not in any corner zone).
    if (on_top    && !in_lc && !in_rc) return SelectionHandle::Top;
    if (on_right  && !in_tc && !in_bc) return SelectionHandle::Right;
    if (on_bottom && !in_lc && !in_rc) return SelectionHandle::Bottom;
    if (on_left   && !in_tc && !in_bc) return SelectionHandle::Left;

    return std::nullopt;
}

RectPx Resize_rect_from_handle(RectPx anchor, SelectionHandle handle,
                               PointPx cursor_px) noexcept {
    RectPx r = anchor.Normalized();
    if (r.Is_empty()) return r;

    switch (handle) {
    case SelectionHandle::TopLeft:
        r.left = cursor_px.x;
        r.top = cursor_px.y;
        break;
    case SelectionHandle::TopRight:
        r.right = cursor_px.x;
        r.top = cursor_px.y;
        break;
    case SelectionHandle::BottomRight:
        r.right = cursor_px.x;
        r.bottom = cursor_px.y;
        break;
    case SelectionHandle::BottomLeft:
        r.left = cursor_px.x;
        r.bottom = cursor_px.y;
        break;
    case SelectionHandle::Top:
        r.top = cursor_px.y;
        break;
    case SelectionHandle::Right:
        r.right = cursor_px.x;
        break;
    case SelectionHandle::Bottom:
        r.bottom = cursor_px.y;
        break;
    case SelectionHandle::Left:
        r.left = cursor_px.x;
        break;
    }

    r = r.Normalized();

    // Enforce minimum size 1x1 (keep the fixed corner/edge, shrink the moving side).
    int const w = r.Width();
    int const height = r.Height();
    if (w < kMinSize) {
        if (handle == SelectionHandle::Left || handle == SelectionHandle::TopLeft ||
            handle == SelectionHandle::BottomLeft) {
            r.left = r.right - kMinSize;
        } else {
            r.right = r.left + kMinSize;
        }
    }
    if (height < kMinSize) {
        if (handle == SelectionHandle::Top || handle == SelectionHandle::TopLeft ||
            handle == SelectionHandle::TopRight) {
            r.top = r.bottom - kMinSize;
        } else {
            r.bottom = r.top + kMinSize;
        }
    }

    return r.Normalized();
}

PointPx Anchor_point_for_resize_policy(RectPx rect, SelectionHandle handle) noexcept {
    RectPx r = rect.Normalized();
    int const cx = (r.left + r.right) / 2;
    int const cy = (r.top + r.bottom) / 2;

    switch (handle) {
    case SelectionHandle::TopLeft:
        return r.Bottom_right();
    case SelectionHandle::TopRight:
        return {r.left, r.bottom};
    case SelectionHandle::BottomRight:
        return r.Top_left();
    case SelectionHandle::BottomLeft:
        return {r.right, r.top};
    case SelectionHandle::Top:
        return {cx, r.bottom};
    case SelectionHandle::Right:
        return {r.left, cy};
    case SelectionHandle::Bottom:
        return {cx, r.top};
    case SelectionHandle::Left:
        return {r.right, cy};
    }
    return {r.left, r.top};
}

} // namespace greenflame::core
