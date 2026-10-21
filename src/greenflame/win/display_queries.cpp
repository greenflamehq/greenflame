#include "win/display_queries.h"

#pragma comment(lib, "Shcore.lib")

namespace greenflame {

core::PointPx Get_cursor_pos_px() {
    POINT pt{};
    if (GetCursorPos(&pt)) {
        return {pt.x, pt.y};
    }
    return {0, 0};
}

core::RectPx Get_virtual_desktop_bounds_px() {
    int const left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int const top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int const width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int const height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return core::Rect_px_from_virtual_screen_metrics(left, top, width, height);
}

namespace {

struct MonitorEnumState {
    std::vector<HMONITOR> handles;
    std::vector<RECT> rects;
    bool had_exception = false;
};

BOOL CALLBACK Enum_monitors_proc(HMONITOR monitor, HDC, LPRECT rect,
                                 LPARAM lparam) noexcept {
    auto *state = reinterpret_cast<MonitorEnumState *>(lparam);
    if (state == nullptr || rect == nullptr) {
        return FALSE;
    }

    try {
        state->handles.push_back(monitor);
        state->rects.push_back(*rect);
        return TRUE;
    } catch (...) {
        state->had_exception = true;
        return FALSE;
    }
}

core::MonitorOrientation Orientation_from_rect(RECT const &rect) noexcept {
    long const width = rect.right - rect.left;
    long const height = rect.bottom - rect.top;
    return (width >= height) ? core::MonitorOrientation::Landscape
                             : core::MonitorOrientation::Portrait;
}

} // namespace

std::vector<core::MonitorWithBounds> Get_monitors_with_bounds() {
    MonitorEnumState state;
    if (!EnumDisplayMonitors(nullptr, nullptr, Enum_monitors_proc,
                             reinterpret_cast<LPARAM>(&state))) {
        return {};
    }
    if (state.had_exception) {
        return {};
    }

    std::vector<core::MonitorWithBounds> out;
    out.reserve(state.handles.size());

    for (size_t i = 0; i < state.handles.size(); ++i) {
        RECT const &rect = state.rects[i];
        core::RectPx const bounds = core::RectPx::From_ltrb(
            static_cast<int32_t>(rect.left), static_cast<int32_t>(rect.top),
            static_cast<int32_t>(rect.right), static_cast<int32_t>(rect.bottom));

        UINT dpi_x = USER_DEFAULT_SCREEN_DPI;
        UINT dpi_y = USER_DEFAULT_SCREEN_DPI;
        (void)GetDpiForMonitor(state.handles[i], MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
        int32_t const percent = core::Dpi_to_scale_percent(static_cast<int>(dpi_x));

        core::MonitorWithBounds monitor{};
        monitor.bounds = bounds;
        monitor.info.dpi_scale.percent = percent;
        monitor.info.orientation = Orientation_from_rect(rect);
        out.push_back(monitor);
    }
    return out;
}

} // namespace greenflame
