#include "monitor_rules.h"

namespace greenflame::core {

namespace {
constexpr int kWindowsDefaultDpi = 96; // 100% scale baseline on Windows.
constexpr int kHalfDefaultDpiForRounding = kWindowsDefaultDpi / 2;
} // namespace

int32_t Dpi_to_scale_percent(int dpi) noexcept {
    return static_cast<int32_t>(
        (static_cast<int64_t>(dpi) * kDefaultMonitorScalePercent +
         kHalfDefaultDpiForRounding) /
        kWindowsDefaultDpi);
}

CrossMonitorSelectionDecision
Decide_cross_monitor_selection(std::span<const MonitorInfo> touched_monitors) noexcept {
    if (touched_monitors.empty()) {
        return CrossMonitorSelectionDecision::RefusedInvalidInput;
    }

    const MonitorInfo &first = touched_monitors.front();

    if (!first.dpi_scale.Is_valid()) {
        return CrossMonitorSelectionDecision::RefusedInvalidInput;
    }

    for (const MonitorInfo &m : touched_monitors) {
        if (!m.dpi_scale.Is_valid()) {
            return CrossMonitorSelectionDecision::RefusedInvalidInput;
        }

        if (!(m.dpi_scale == first.dpi_scale)) {
            return CrossMonitorSelectionDecision::RefusedDifferentDpiScale;
        }

        if (m.orientation != first.orientation) {
            return CrossMonitorSelectionDecision::RefusedDifferentOrientation;
        }
    }

    return CrossMonitorSelectionDecision::Allowed;
}

std::optional<size_t>
Index_of_monitor_containing(PointPx p,
                            std::span<const MonitorWithBounds> monitors) noexcept {
    for (size_t i = 0; i < monitors.size(); ++i) {
        if (monitors[i].bounds.Contains(p)) return i;
    }
    return std::nullopt;
}

std::vector<size_t>
Indices_of_monitors_intersecting(RectPx r,
                                 std::span<const MonitorWithBounds> monitors) noexcept {
    std::vector<size_t> out;
    r = r.Normalized();
    for (size_t i = 0; i < monitors.size(); ++i) {
        auto inter = RectPx::Intersect(r, monitors[i].bounds.Normalized());
        if (inter.has_value() && !inter->Is_empty()) out.push_back(i);
    }
    return out;
}

std::vector<RectPx> Monitor_rects_in_client(std::span<const MonitorWithBounds> monitors,
                                            PointPx client_origin_screen) {
    std::vector<RectPx> out;
    out.reserve(monitors.size());
    for (MonitorWithBounds const &monitor : monitors) {
        out.push_back(
            RectPx::From_ltrb(monitor.bounds.left - client_origin_screen.x,
                              monitor.bounds.top - client_origin_screen.y,
                              monitor.bounds.right - client_origin_screen.x,
                              monitor.bounds.bottom - client_origin_screen.y));
    }
    return out;
}

RectPx Allowed_selection_rect(RectPx candidate, PointPx start,
                              std::span<const MonitorWithBounds> monitors) noexcept {
    if (candidate.Is_empty()) return candidate;
    if (monitors.empty()) return candidate;

    const std::vector<size_t> touched =
        Indices_of_monitors_intersecting(candidate, monitors);
    if (touched.empty()) return candidate;

    std::vector<MonitorInfo> touched_infos;
    touched_infos.reserve(touched.size());
    for (size_t i : touched) {
        touched_infos.push_back(monitors[i].info);
    }

    if (Is_allowed(Decide_cross_monitor_selection(touched_infos))) return candidate;

    const std::optional<size_t> start_idx =
        Index_of_monitor_containing(start, monitors);
    const size_t clamp_idx =
        start_idx.has_value()
            ? *start_idx
            : touched[0]; // start outside all: clamp to first touched monitor

    const RectPx clamp_bounds = monitors[clamp_idx].bounds.Normalized();
    auto inter = RectPx::Intersect(candidate.Normalized(), clamp_bounds);
    if (!inter.has_value()) return RectPx::From_ltrb(0, 0, 0, 0);
    return *inter;
}
} // namespace greenflame::core
