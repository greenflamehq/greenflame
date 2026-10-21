#include "greenflame_core/overlay_surface_layout.h"

namespace greenflame::core {

namespace {

// Squared distance from point to the nearest pixel of rect (0 inside it).
[[nodiscard]] int64_t Squared_distance(RectPx rect, PointPx point) noexcept {
    RectPx const r = rect.Normalized();
    int64_t const dx = point.x < r.left ? static_cast<int64_t>(r.left) - point.x
                       : point.x >= r.right
                           ? static_cast<int64_t>(point.x) - (r.right - 1)
                           : 0;
    int64_t const dy = point.y < r.top ? static_cast<int64_t>(r.top) - point.y
                       : point.y >= r.bottom
                           ? static_cast<int64_t>(point.y) - (r.bottom - 1)
                           : 0;
    return dx * dx + dy * dy;
}

[[nodiscard]] std::vector<RectPx> Sorted_rects(std::vector<RectPx> rects) {
    std::sort(rects.begin(), rects.end(), [](RectPx const &a, RectPx const &b) {
        if (a.left != b.left) return a.left < b.left;
        if (a.top != b.top) return a.top < b.top;
        if (a.right != b.right) return a.right < b.right;
        return a.bottom < b.bottom;
    });
    return rects;
}

} // namespace

size_t Index_of_rect_nearest(std::span<const RectPx> rects, PointPx point) noexcept {
    size_t best = 0;
    int64_t best_distance = std::numeric_limits<int64_t>::max();
    for (size_t i = 0; i < rects.size(); ++i) {
        int64_t const distance = Squared_distance(rects[i], point);
        if (distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

OverlayLayout Plan_overlay_layout(std::span<const MonitorWithBounds> monitors,
                                  RectPx desktop_screen, PointPx cursor_screen,
                                  bool per_monitor) {
    OverlayLayout layout{};
    layout.desktop_screen = desktop_screen;
    auto const to_client = [&](RectPx screen) {
        return RectPx::From_ltrb(
            screen.left - desktop_screen.left, screen.top - desktop_screen.top,
            screen.right - desktop_screen.left, screen.bottom - desktop_screen.top);
    };
    if (per_monitor) {
        for (MonitorWithBounds const &monitor : monitors) {
            std::optional<RectPx> const inside =
                RectPx::Clip(monitor.bounds.Normalized(), desktop_screen);
            if (inside.has_value()) {
                layout.surfaces.push_back({*inside, to_client(*inside)});
            }
        }
    }
    layout.per_monitor = !layout.surfaces.empty();
    if (!layout.per_monitor) {
        layout.surfaces.push_back({desktop_screen, to_client(desktop_screen)});
    }
    std::vector<RectPx> screens;
    screens.reserve(layout.surfaces.size());
    for (OverlaySurfacePlan const &surface : layout.surfaces) {
        screens.push_back(surface.rect_screen);
    }
    layout.primary = Index_of_rect_nearest(screens, cursor_screen);
    return layout;
}

bool Surface_matches_plan(RectPx planned_screen, RectPx window_rect_screen,
                          SizePx client_size) noexcept {
    return !planned_screen.Is_empty() && window_rect_screen == planned_screen &&
           client_size.w == planned_screen.Width() &&
           client_size.h == planned_screen.Height();
}

bool Layout_still_matches(OverlayLayout const &layout,
                          std::span<const MonitorWithBounds> monitors,
                          RectPx desktop_screen) {
    if (desktop_screen != layout.desktop_screen) {
        return false;
    }
    if (!layout.per_monitor) {
        return true;
    }
    std::vector<RectPx> planned;
    for (OverlaySurfacePlan const &surface : layout.surfaces) {
        planned.push_back(surface.rect_screen);
    }
    std::vector<RectPx> now;
    for (MonitorWithBounds const &monitor : monitors) {
        now.push_back(monitor.bounds.Normalized());
    }
    return Sorted_rects(std::move(planned)) == Sorted_rects(std::move(now));
}

} // namespace greenflame::core
