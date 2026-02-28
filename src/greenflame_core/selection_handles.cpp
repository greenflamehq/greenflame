#include "selection_handles.h"

namespace greenflame::core {

namespace {

constexpr int32_t kMinSize = 1;

bool Within_radius_sq(PointPx a, PointPx b, int radius_px) noexcept {
    if (radius_px <= 0) return false;
    const int64_t dx = static_cast<int64_t>(a.x) - static_cast<int64_t>(b.x);
    const int64_t dy = static_cast<int64_t>(a.y) - static_cast<int64_t>(b.y);
    const int64_t r = static_cast<int64_t>(radius_px);
    return dx * dx + dy * dy <= r * r;
}

#pragma warning(push)
#pragma warning(disable : 4061)
CLANG_WARN_IGNORE_PUSH("-Wswitch-enum")
PointPx Corner_position(RectPx const &r, SelectionHandle h) noexcept {
    switch (h) {
    case SelectionHandle::TopLeft:
        return r.Top_left();
    case SelectionHandle::TopRight:
        return {r.right, r.top};
    case SelectionHandle::BottomRight:
        return r.Bottom_right();
    case SelectionHandle::BottomLeft:
        return {r.left, r.bottom};
    default:
        return {0, 0};
    }
}

PointPx Edge_midpoint_position(RectPx const &r, SelectionHandle h) noexcept {
    int const cx = (r.left + r.right) / 2;
    int const cy = (r.top + r.bottom) / 2;
    switch (h) {
    case SelectionHandle::Top:
        return {cx, r.top};
    case SelectionHandle::Right:
        return {r.right, cy};
    case SelectionHandle::Bottom:
        return {cx, r.bottom};
    case SelectionHandle::Left:
        return {r.left, cy};
    default:
        return {0, 0};
    }
}
CLANG_WARN_IGNORE_POP()
#pragma warning(pop)
} // namespace

std::optional<SelectionHandle> Hit_test_selection_handle(RectPx selection,
                                                         PointPx cursor_client_px,
                                                         int grab_radius_px) noexcept {
    RectPx const r = selection.Normalized();
    if (r.Is_empty()) return std::nullopt;

    // Corners first (priority over edges).
    static constexpr SelectionHandle kCorners[] = {
        SelectionHandle::TopLeft, SelectionHandle::TopRight,
        SelectionHandle::BottomRight, SelectionHandle::BottomLeft};
    for (SelectionHandle h : kCorners) {
        if (Within_radius_sq(Corner_position(r, h), cursor_client_px, grab_radius_px)) {
            return h;
        }
    }

    // Then edges.
    static constexpr SelectionHandle kEdges[] = {
        SelectionHandle::Top, SelectionHandle::Right, SelectionHandle::Bottom,
        SelectionHandle::Left};
    for (SelectionHandle h : kEdges) {
        if (Within_radius_sq(Edge_midpoint_position(r, h), cursor_client_px,
                             grab_radius_px)) {
            return h;
        }
    }

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
