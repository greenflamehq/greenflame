#include "win/window_query.h"

namespace greenflame {

namespace {

[[nodiscard]] bool Is_empty_rect(RECT const &rect) noexcept {
    return rect.left >= rect.right || rect.top >= rect.bottom;
}

void Delete_region(HRGN region) noexcept {
    if (region != nullptr) {
        DeleteObject(region);
    }
}

[[nodiscard]] bool Is_window_cloaked(HWND hwnd) noexcept {
    DWORD cloaked = 0;
    HRESULT const hr =
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != 0;
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
    return IsWindow(hwnd) != 0 && IsWindowVisible(hwnd) != 0 &&
           GetParent(hwnd) == nullptr && IsIconic(hwnd) == 0 &&
           !Is_window_cloaked(hwnd);
}

[[nodiscard]] HRGN Build_visible_region(RECT const &target,
                                        std::span<const RECT> occluders) noexcept {
    if (Is_empty_rect(target)) {
        return nullptr;
    }

    HRGN visible = CreateRectRgnIndirect(&target);
    HRGN scratch = CreateRectRgn(0, 0, 0, 0);
    if (visible == nullptr || scratch == nullptr) {
        Delete_region(visible);
        Delete_region(scratch);
        return nullptr;
    }

    for (RECT const &occluder : occluders) {
        if (Is_empty_rect(occluder)) {
            continue;
        }
        HRGN occluder_region = CreateRectRgnIndirect(&occluder);
        if (occluder_region == nullptr) {
            continue;
        }
        int const result = CombineRgn(scratch, visible, occluder_region, RGN_DIFF);
        Delete_region(occluder_region);
        if (result == ERROR) {
            continue;
        }
        std::swap(visible, scratch);
        if (result == NULLREGION) {
            break;
        }
    }

    Delete_region(scratch);
    return visible;
}

[[nodiscard]] bool Is_fully_occluded(RECT const &target,
                                     std::span<const RECT> occluders) noexcept {
    if (occluders.empty()) {
        return false;
    }

    HRGN const visible = Build_visible_region(target, occluders);
    if (visible == nullptr) {
        return false;
    }

    RECT remaining{};
    bool const fully_occluded = GetRgnBox(visible, &remaining) == NULLREGION;
    Delete_region(visible);
    return fully_occluded;
}

void Merge_segments(std::vector<greenflame::core::SnapEdgeSegmentPx> &segments) {
    std::sort(segments.begin(), segments.end(),
              [](greenflame::core::SnapEdgeSegmentPx const &lhs,
                 greenflame::core::SnapEdgeSegmentPx const &rhs) {
                  if (lhs.line != rhs.line) {
                      return lhs.line < rhs.line;
                  }
                  if (lhs.span_start != rhs.span_start) {
                      return lhs.span_start < rhs.span_start;
                  }
                  return lhs.span_end < rhs.span_end;
              });

    std::vector<greenflame::core::SnapEdgeSegmentPx> merged;
    merged.reserve(segments.size());
    for (greenflame::core::SnapEdgeSegmentPx segment : segments) {
        segment = segment.Normalized();
        if (segment.Is_empty()) {
            continue;
        }
        if (merged.empty() || merged.back().line != segment.line ||
            merged.back().span_end < segment.span_start) {
            merged.push_back(segment);
            continue;
        }
        merged.back().span_end = std::max(merged.back().span_end, segment.span_end);
    }
    segments = std::move(merged);
}

void Collect_region_rects(HRGN region, std::vector<RECT> &out) {
    DWORD const bytes = GetRegionData(region, 0, nullptr);
    size_t const header_bytes = offsetof(RGNDATA, Buffer);
    if (bytes < header_bytes) {
        return;
    }

    std::vector<std::byte> buffer(bytes);

    CLANG_WARN_IGNORE_PUSH("-Wunsafe-buffer-usage")
    RGNDATA *const data = reinterpret_cast<RGNDATA *>(buffer.data());
    CLANG_WARN_IGNORE_POP()
    if (GetRegionData(region, bytes, data) == 0) {
        return;
    }

    size_t const available_rect_count =
        (static_cast<size_t>(bytes) - header_bytes) / sizeof(RECT);
    size_t const rect_count =
        std::min(static_cast<size_t>(data->rdh.nCount), available_rect_count);
    size_t const out_offset = out.size();
    out.resize(out_offset + rect_count);
    std::copy_n(reinterpret_cast<RECT const *>(data->Buffer), rect_count,
                out.begin() +
                    static_cast<std::vector<RECT>::difference_type>(out_offset));
}

void Append_edge_segments_from_strip(
    HRGN visible_region, RECT const &strip_rect, bool vertical, int32_t line,
    std::vector<greenflame::core::SnapEdgeSegmentPx> &out) {
    if (visible_region == nullptr || Is_empty_rect(strip_rect)) {
        return;
    }

    HRGN strip_region = CreateRectRgnIndirect(&strip_rect);
    HRGN intersection = CreateRectRgn(0, 0, 0, 0);
    if (strip_region == nullptr || intersection == nullptr) {
        Delete_region(strip_region);
        Delete_region(intersection);
        return;
    }

    int const result = CombineRgn(intersection, visible_region, strip_region, RGN_AND);
    Delete_region(strip_region);
    if (result == ERROR || result == NULLREGION) {
        Delete_region(intersection);
        return;
    }

    std::vector<RECT> region_rects;
    Collect_region_rects(intersection, region_rects);
    Delete_region(intersection);

    std::vector<greenflame::core::SnapEdgeSegmentPx> segments;
    segments.reserve(region_rects.size());
    for (RECT const &rect : region_rects) {
        if (vertical) {
            segments.push_back({line, static_cast<int32_t>(rect.top),
                                static_cast<int32_t>(rect.bottom)});
        } else {
            segments.push_back({line, static_cast<int32_t>(rect.left),
                                static_cast<int32_t>(rect.right)});
        }
    }

    Merge_segments(segments);
    out.insert(out.end(), segments.begin(), segments.end());
}

void Append_visible_window_snap_edges(RECT const &window_rect, HRGN visible_region,
                                      greenflame::core::SnapEdges &out) {
    RECT const left_strip = {window_rect.left, window_rect.top, window_rect.left + 1,
                             window_rect.bottom};
    RECT const right_strip = {window_rect.right - 1, window_rect.top, window_rect.right,
                              window_rect.bottom};
    RECT const top_strip = {window_rect.left, window_rect.top, window_rect.right,
                            window_rect.top + 1};
    RECT const bottom_strip = {window_rect.left, window_rect.bottom - 1,
                               window_rect.right, window_rect.bottom};

    Append_edge_segments_from_strip(visible_region, left_strip, true,
                                    static_cast<int32_t>(window_rect.left),
                                    out.vertical);
    Append_edge_segments_from_strip(visible_region, right_strip, true,
                                    static_cast<int32_t>(window_rect.right),
                                    out.vertical);
    Append_edge_segments_from_strip(visible_region, top_strip, false,
                                    static_cast<int32_t>(window_rect.top),
                                    out.horizontal);
    Append_edge_segments_from_strip(visible_region, bottom_strip, false,
                                    static_cast<int32_t>(window_rect.bottom),
                                    out.horizontal);
}

} // namespace

bool Is_window_excluded_from_capture(HWND hwnd) noexcept {
    DWORD affinity = WDA_NONE;
    return GetWindowDisplayAffinity(hwnd, &affinity) != 0 &&
           affinity == WDA_EXCLUDEFROMCAPTURE;
}

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
        if (Is_visible_top_level_window(hwnd) &&
            !Is_window_excluded_from_capture(hwnd) &&
            Try_get_window_bounds(hwnd, rect) && PtInRect(&rect, screen_pt)) {
            return hwnd;
        }
        hwnd = GetWindow(hwnd, GW_HWNDNEXT);
    }
    return std::nullopt;
}

std::optional<greenflame::core::RectPx>
Win32WindowQuery::Get_window_rect(HWND hwnd) const {
    if (hwnd == nullptr || !Is_visible_top_level_window(hwnd)) {
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
    if (window == nullptr || window == exclude_hwnd ||
        !Is_visible_top_level_window(window) ||
        Is_window_excluded_from_capture(window)) {
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

void Win32WindowQuery::Get_visible_top_level_window_snap_edges(
    HWND exclude_hwnd, greenflame::core::SnapEdges &out) const {
    HWND hwnd = GetWindow(exclude_hwnd, GW_HWNDNEXT);
    std::vector<RECT> occluders;
    while (hwnd != nullptr) {
        RECT rect{};
        // Uncapturable windows are excluded from both snap edges and the
        // occluder list: they render no visible content, so they should not
        // clip snap edges of windows behind them.
        if (Is_visible_top_level_window(hwnd) &&
            !Is_window_excluded_from_capture(hwnd) &&
            Try_get_window_bounds(hwnd, rect)) {
            HRGN const visible_region = Build_visible_region(rect, occluders);
            if (visible_region != nullptr) {
                Append_visible_window_snap_edges(rect, visible_region, out);
                Delete_region(visible_region);
            }
            occluders.push_back(rect);
        }
        hwnd = GetWindow(hwnd, GW_HWNDNEXT);
    }
}

WindowObscuration Win32WindowQuery::Get_window_obscuration(HWND hwnd) const {
    if (hwnd == nullptr || !Is_visible_top_level_window(hwnd)) {
        return WindowObscuration::None;
    }

    RECT target_rect{};
    if (!Try_get_window_bounds(hwnd, target_rect)) {
        return WindowObscuration::None;
    }

    std::vector<RECT> occluders;
    HWND scan = GetTopWindow(nullptr);
    while (scan != nullptr && scan != hwnd) {
        if (Is_visible_top_level_window(scan)) {
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
