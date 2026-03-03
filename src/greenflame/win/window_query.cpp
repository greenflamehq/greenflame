#include "win/window_query.h"

namespace greenflame {

namespace {

[[nodiscard]] bool Is_empty_rect(RECT const &rect) noexcept {
    return rect.left >= rect.right || rect.top >= rect.bottom;
}

[[nodiscard]] bool Try_get_window_bounds(HWND hwnd, RECT &out_rect) noexcept {
    HRESULT const hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                             &out_rect, sizeof(out_rect));
    if (!SUCCEEDED(hr)) {
        if (GetWindowRect(hwnd, &out_rect) == 0) {
            return false;
        }
    }
    return !Is_empty_rect(out_rect);
}

[[nodiscard]] bool Is_visible_top_level_window(HWND hwnd) noexcept {
    return IsWindowVisible(hwnd) && GetParent(hwnd) == nullptr;
}

[[nodiscard]] bool Is_fully_occluded(RECT const &target,
                                     std::vector<RECT> const &occluders) noexcept {
    if (occluders.empty()) {
        return false;
    }

    HRGN visible = CreateRectRgnIndirect(&target);
    HRGN scratch = CreateRectRgn(0, 0, 0, 0);
    if (!visible || !scratch) {
        if (visible) {
            DeleteObject(visible);
        }
        if (scratch) {
            DeleteObject(scratch);
        }
        return false;
    }

    bool fully_occluded = false;
    for (RECT const &occluder : occluders) {
        if (Is_empty_rect(occluder)) {
            continue;
        }
        HRGN occluder_rgn = CreateRectRgnIndirect(&occluder);
        if (!occluder_rgn) {
            continue;
        }
        CombineRgn(scratch, visible, occluder_rgn, RGN_DIFF);
        DeleteObject(occluder_rgn);
        std::swap(visible, scratch);

        RECT remaining{};
        if (GetRgnBox(visible, &remaining) == NULLREGION) {
            fully_occluded = true;
            break;
        }
    }

    DeleteObject(visible);
    DeleteObject(scratch);
    return fully_occluded;
}

} // namespace

std::optional<HWND> Win32WindowQuery::Get_window_under_cursor(POINT screen_pt,
                                                              HWND exclude_hwnd) const {
    HWND hwnd = nullptr;
    if (exclude_hwnd != nullptr) {
        hwnd = GetWindow(exclude_hwnd, GW_HWNDNEXT);
    } else {
        hwnd = GetTopWindow(nullptr);
    }
    while (hwnd != nullptr) {
        RECT rect{};
        if (Is_visible_top_level_window(hwnd) && Try_get_window_bounds(hwnd, rect) &&
            PtInRect(&rect, screen_pt)) {
            return hwnd;
        }
        hwnd = GetWindow(hwnd, GW_HWNDNEXT);
    }
    return std::nullopt;
}

std::optional<greenflame::core::RectPx>
Win32WindowQuery::Get_window_rect(HWND hwnd) const {
    if (hwnd == nullptr) {
        return std::nullopt;
    }
    if (!IsWindowVisible(hwnd) || GetParent(hwnd) != nullptr) {
        return std::nullopt;
    }
    RECT rect{};
    if (!Try_get_window_bounds(hwnd, rect)) {
        return std::nullopt;
    }
    return greenflame::core::RectPx::From_ltrb(
        static_cast<int32_t>(rect.left), static_cast<int32_t>(rect.top),
        static_cast<int32_t>(rect.right), static_cast<int32_t>(rect.bottom));
}

std::optional<greenflame::core::RectPx>
Win32WindowQuery::Get_foreground_window_rect(HWND exclude_hwnd) const {
    HWND const window = GetForegroundWindow();
    if (window == nullptr || window == exclude_hwnd) {
        return std::nullopt;
    }
    if (!IsWindowVisible(window) || GetParent(window) != nullptr) {
        return std::nullopt;
    }

    RECT rect{};
    if (!Try_get_window_bounds(window, rect)) {
        return std::nullopt;
    }
    return greenflame::core::RectPx::From_ltrb(
        static_cast<int32_t>(rect.left), static_cast<int32_t>(rect.top),
        static_cast<int32_t>(rect.right), static_cast<int32_t>(rect.bottom));
}

std::optional<greenflame::core::RectPx>
Win32WindowQuery::Get_window_rect_under_cursor(POINT screen_pt,
                                               HWND exclude_hwnd) const {
    std::optional<HWND> window = Get_window_under_cursor(screen_pt, exclude_hwnd);
    if (!window.has_value()) {
        return std::nullopt;
    }
    RECT rect{};
    if (!Try_get_window_bounds(*window, rect)) {
        return std::nullopt;
    }
    return greenflame::core::RectPx::From_ltrb(
        static_cast<int32_t>(rect.left), static_cast<int32_t>(rect.top),
        static_cast<int32_t>(rect.right), static_cast<int32_t>(rect.bottom));
}

void Win32WindowQuery::Get_visible_top_level_window_rects(
    HWND exclude_hwnd, std::vector<greenflame::core::RectPx> &out) const {
    HWND hwnd = GetWindow(exclude_hwnd, GW_HWNDNEXT);
    std::vector<RECT> occluders;
    while (hwnd != nullptr) {
        RECT rect{};
        if (Is_visible_top_level_window(hwnd) && Try_get_window_bounds(hwnd, rect)) {
            if (!Is_fully_occluded(rect, occluders)) {
                out.push_back(greenflame::core::RectPx::From_ltrb(
                    static_cast<int32_t>(rect.left), static_cast<int32_t>(rect.top),
                    static_cast<int32_t>(rect.right),
                    static_cast<int32_t>(rect.bottom)));
            }
            occluders.push_back(rect);
        }
        hwnd = GetWindow(hwnd, GW_HWNDNEXT);
    }
}

WindowObscuration Win32WindowQuery::Get_window_obscuration(HWND hwnd) const {
    if (hwnd == nullptr) {
        return WindowObscuration::None;
    }
    if (IsWindow(hwnd) == 0 || !IsWindowVisible(hwnd) || GetParent(hwnd) != nullptr ||
        IsIconic(hwnd) != 0) {
        return WindowObscuration::None;
    }

    RECT target_rect{};
    if (!Try_get_window_bounds(hwnd, target_rect)) {
        return WindowObscuration::None;
    }

    std::vector<RECT> occluders;
    HWND scan = GetTopWindow(nullptr);
    while (scan != nullptr && scan != hwnd) {
        if (IsWindowVisible(scan) != 0 && GetParent(scan) == nullptr &&
            IsIconic(scan) == 0) {
            RECT occluder_rect{};
            if (Try_get_window_bounds(scan, occluder_rect)) {
                occluders.push_back(occluder_rect);
            }
        }
        scan = GetWindow(scan, GW_HWNDNEXT);
    }

    if (scan == nullptr) {
        return WindowObscuration::None;
    }

    bool has_overlap = false;
    for (RECT const &occluder_rect : occluders) {
        RECT overlap{};
        if (IntersectRect(&overlap, &target_rect, &occluder_rect) != 0 &&
            !Is_empty_rect(overlap)) {
            has_overlap = true;
            break;
        }
    }

    if (!has_overlap) {
        return WindowObscuration::None;
    }
    if (Is_fully_occluded(target_rect, occluders)) {
        return WindowObscuration::Full;
    }
    return WindowObscuration::Partial;
}

} // namespace greenflame
