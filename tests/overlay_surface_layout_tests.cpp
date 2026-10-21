#include "greenflame_core/overlay_surface_layout.h"

using namespace greenflame::core;

namespace {

MonitorWithBounds Monitor_at(int32_t left, int32_t top, int32_t right, int32_t bottom) {
    return MonitorWithBounds{RectPx::From_ltrb(left, top, right, bottom),
                             MonitorInfo{}};
}

// Jocelyn's desk: laptop left of the primary and lower, two 2560x1440 panels.
std::vector<MonitorWithBounds> Jocelyn_desk() {
    return {Monitor_at(-1920, 137, 0, 1337), Monitor_at(0, 0, 2560, 1440),
            Monitor_at(2560, 0, 5120, 1440)};
}

RectPx const kJocelynDesktop = RectPx::From_ltrb(-1920, 0, 5120, 1440);

} // namespace

TEST(overlay_surface_layout,
     PerMonitor_JocelynDesk_OneSurfacePerMonitorInClientPixels) {
    OverlayLayout const layout =
        Plan_overlay_layout(Jocelyn_desk(), kJocelynDesktop, PointPx{100, 100}, true);
    EXPECT_TRUE(layout.per_monitor);
    EXPECT_EQ(layout.desktop_screen, kJocelynDesktop);
    ASSERT_EQ(layout.surfaces.size(), 3u);
    EXPECT_EQ(layout.surfaces[0].rect_screen, RectPx::From_ltrb(-1920, 137, 0, 1337));
    EXPECT_EQ(layout.surfaces[0].rect_client, RectPx::From_ltrb(0, 137, 1920, 1337));
    EXPECT_EQ(layout.surfaces[1].rect_client, RectPx::From_ltrb(1920, 0, 4480, 1440));
    EXPECT_EQ(layout.surfaces[2].rect_client, RectPx::From_ltrb(4480, 0, 7040, 1440));
    EXPECT_EQ(layout.primary, 1u);
}

TEST(overlay_surface_layout, PerMonitor_MonitorAbovePrimary_NegativeY) {
    std::vector<MonitorWithBounds> const monitors = {Monitor_at(0, 0, 1920, 1080),
                                                     Monitor_at(0, -1440, 2560, 0)};
    RectPx const desktop = RectPx::From_ltrb(0, -1440, 2560, 1080);
    OverlayLayout const layout =
        Plan_overlay_layout(monitors, desktop, PointPx{10, -10}, true);
    ASSERT_EQ(layout.surfaces.size(), 2u);
    EXPECT_EQ(layout.surfaces[0].rect_client, RectPx::From_ltrb(0, 1440, 1920, 2520));
    EXPECT_EQ(layout.surfaces[1].rect_client, RectPx::From_ltrb(0, 0, 2560, 1440));
    EXPECT_EQ(layout.primary, 1u);
}

TEST(overlay_surface_layout, PerMonitor_Portrait) {
    std::vector<MonitorWithBounds> const monitors = {Monitor_at(0, 0, 2560, 1440),
                                                     Monitor_at(2560, 0, 3640, 1920)};
    RectPx const desktop = RectPx::From_ltrb(0, 0, 3640, 1920);
    OverlayLayout const layout =
        Plan_overlay_layout(monitors, desktop, PointPx{3000, 1800}, true);
    ASSERT_EQ(layout.surfaces.size(), 2u);
    EXPECT_EQ(layout.surfaces[1].rect_client, RectPx::From_ltrb(2560, 0, 3640, 1920));
    EXPECT_EQ(layout.primary, 1u);
}

TEST(overlay_surface_layout, SingleMonitor_PerMonitorEqualsSpan) {
    std::vector<MonitorWithBounds> const monitors = {Monitor_at(0, 0, 1920, 1080)};
    RectPx const desktop = RectPx::From_ltrb(0, 0, 1920, 1080);
    OverlayLayout const per_monitor =
        Plan_overlay_layout(monitors, desktop, PointPx{5, 5}, true);
    OverlayLayout const span =
        Plan_overlay_layout(monitors, desktop, PointPx{5, 5}, false);
    EXPECT_EQ(per_monitor.surfaces, span.surfaces);
    EXPECT_EQ(per_monitor.primary, 0u);
}

TEST(overlay_surface_layout, Span_OneSurfaceOverTheDesktop) {
    OverlayLayout const layout =
        Plan_overlay_layout(Jocelyn_desk(), kJocelynDesktop, PointPx{100, 100}, false);
    EXPECT_FALSE(layout.per_monitor);
    ASSERT_EQ(layout.surfaces.size(), 1u);
    EXPECT_EQ(layout.surfaces[0].rect_screen, kJocelynDesktop);
    EXPECT_EQ(layout.surfaces[0].rect_client, RectPx::From_ltrb(0, 0, 7040, 1440));
    EXPECT_EQ(layout.primary, 0u);
}

TEST(overlay_surface_layout, NoMonitors_FallsBackToSpan) {
    OverlayLayout const layout =
        Plan_overlay_layout({}, kJocelynDesktop, PointPx{100, 100}, true);
    EXPECT_FALSE(layout.per_monitor);
    ASSERT_EQ(layout.surfaces.size(), 1u);
    EXPECT_EQ(layout.surfaces[0].rect_screen, kJocelynDesktop);
}

// The laptop's top-left strip (y < 137, x < 0) is inside the desktop's bounding box
// but on no monitor: the nearest monitor is the laptop.
TEST(overlay_surface_layout, Primary_CursorInGap_PicksNearest) {
    OverlayLayout const layout =
        Plan_overlay_layout(Jocelyn_desk(), kJocelynDesktop, PointPx{-1000, 10}, true);
    EXPECT_EQ(layout.primary, 0u);
}

// Monitor rects are half-open: x = 0 is the primary's first column, not the laptop's.
TEST(overlay_surface_layout, Primary_CursorOnSharedEdge) {
    EXPECT_EQ(
        Plan_overlay_layout(Jocelyn_desk(), kJocelynDesktop, PointPx{0, 500}, true)
            .primary,
        1u);
    EXPECT_EQ(
        Plan_overlay_layout(Jocelyn_desk(), kJocelynDesktop, PointPx{-1, 500}, true)
            .primary,
        0u);
    EXPECT_EQ(
        Plan_overlay_layout(Jocelyn_desk(), kJocelynDesktop, PointPx{2560, 0}, true)
            .primary,
        2u);
}

TEST(overlay_surface_layout, Primary_CursorOutsideEveryMonitor_Nearest) {
    // Pixel columns 0-99 and 201-299: x = 150 is 51 px from each.
    std::array<RectPx, 2> const rects = {RectPx::From_ltrb(0, 0, 100, 100),
                                         RectPx::From_ltrb(201, 0, 300, 100)};
    EXPECT_EQ(Index_of_rect_nearest(rects, PointPx{180, 50}), 1u);
    EXPECT_EQ(Index_of_rect_nearest(rects, PointPx{120, 50}), 0u);
    EXPECT_EQ(Index_of_rect_nearest(rects, PointPx{150, 50}), 0u); // tie: first
    EXPECT_EQ(Index_of_rect_nearest({}, PointPx{0, 0}), 0u);
}

TEST(overlay_surface_layout, Surface_matches_plan) {
    RectPx const planned = RectPx::From_ltrb(-1920, 137, 0, 1337);
    EXPECT_TRUE(Surface_matches_plan(planned, planned, SizePx{1920, 1200}));
    EXPECT_FALSE(Surface_matches_plan(planned, RectPx::From_ltrb(-1919, 137, 1, 1337),
                                      SizePx{1920, 1200}));
    EXPECT_FALSE(Surface_matches_plan(planned, RectPx::From_ltrb(-1920, 137, 1, 1337),
                                      SizePx{1921, 1200}));
    EXPECT_FALSE(Surface_matches_plan(planned, planned, SizePx{1920, 1199}));
    EXPECT_FALSE(Surface_matches_plan(RectPx{}, RectPx{}, SizePx{}));
}

TEST(overlay_surface_layout, Layout_still_matches) {
    OverlayLayout const per_monitor =
        Plan_overlay_layout(Jocelyn_desk(), kJocelynDesktop, PointPx{100, 100}, true);
    std::vector<MonitorWithBounds> reordered = Jocelyn_desk();
    std::reverse(reordered.begin(), reordered.end());
    EXPECT_TRUE(Layout_still_matches(per_monitor, reordered, kJocelynDesktop));

    std::vector<MonitorWithBounds> unplugged = Jocelyn_desk();
    unplugged.pop_back();
    EXPECT_FALSE(Layout_still_matches(per_monitor, unplugged,
                                      RectPx::From_ltrb(-1920, 0, 2560, 1440)));

    // Same desktop box, laptop moved down: per-monitor notices, span does not.
    std::vector<MonitorWithBounds> moved = Jocelyn_desk();
    moved[0] = Monitor_at(-1920, 200, 0, 1400);
    EXPECT_FALSE(Layout_still_matches(per_monitor, moved, kJocelynDesktop));
    OverlayLayout const span =
        Plan_overlay_layout(Jocelyn_desk(), kJocelynDesktop, PointPx{100, 100}, false);
    EXPECT_TRUE(Layout_still_matches(span, moved, kJocelynDesktop));
    EXPECT_FALSE(
        Layout_still_matches(span, moved, RectPx::From_ltrb(-1920, 0, 5120, 1441)));
}
