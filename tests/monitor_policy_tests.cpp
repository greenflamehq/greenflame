#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"

using namespace greenflame::core;

// Fixture: two monitors side-by-side, same DPI and orientation.
static std::vector<MonitorWithBounds> Two_monitors_same_dpi() {
    MonitorInfo info{MonitorDpiScale{150}, MonitorOrientation::Landscape};
    std::vector<MonitorWithBounds> out;
    out.push_back(MonitorWithBounds{RectPx::From_ltrb(0, 0, 1920, 1080), info});
    out.push_back(MonitorWithBounds{RectPx::From_ltrb(1920, 0, 3840, 1080), info});
    return out;
}

// Fixture: two monitors with different DPI.
static std::vector<MonitorWithBounds> Two_monitors_different_dpi() {
    std::vector<MonitorWithBounds> out;
    out.push_back(MonitorWithBounds{
        RectPx::From_ltrb(0, 0, 1920, 1080),
        MonitorInfo{MonitorDpiScale{150}, MonitorOrientation::Landscape}});
    out.push_back(MonitorWithBounds{
        RectPx::From_ltrb(1920, 0, 3840, 1080),
        MonitorInfo{MonitorDpiScale{125}, MonitorOrientation::Landscape}});
    return out;
}

// Fixture: two monitors with different orientation.
static std::vector<MonitorWithBounds> Two_monitors_different_orientation() {
    std::vector<MonitorWithBounds> out;
    out.push_back(MonitorWithBounds{
        RectPx::From_ltrb(0, 0, 1920, 1080),
        MonitorInfo{MonitorDpiScale{100}, MonitorOrientation::Landscape}});
    out.push_back(MonitorWithBounds{
        RectPx::From_ltrb(0, 1080, 1080, 1920 + 1080),
        MonitorInfo{MonitorDpiScale{100}, MonitorOrientation::Portrait}});
    return out;
}

TEST(monitor_policy, Index_of_monitor_containing_PointInFirstMonitor) {
    auto monitors = Two_monitors_same_dpi();
    auto idx = Index_of_monitor_containing(PointPx{100, 100}, monitors);
    EXPECT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0u);
}

TEST(monitor_policy, Index_of_monitor_containing_PointInSecondMonitor) {
    auto monitors = Two_monitors_same_dpi();
    auto idx = Index_of_monitor_containing(PointPx{2000, 500}, monitors);
    EXPECT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 1u);
}

TEST(monitor_policy, Index_of_monitor_containing_PointOutsideAllMonitors) {
    auto monitors = Two_monitors_same_dpi();
    auto idx = Index_of_monitor_containing(PointPx{-100, -100}, monitors);
    EXPECT_FALSE(idx.has_value());
}

TEST(monitor_policy, Indices_of_monitors_intersecting_RectInFirstMonitorOnly) {
    auto monitors = Two_monitors_same_dpi();
    RectPx r = RectPx::From_ltrb(100, 100, 500, 500);
    auto indices = Indices_of_monitors_intersecting(r, monitors);
    EXPECT_EQ(indices.size(), 1u);
    EXPECT_EQ(indices[0], 0u);
}

TEST(monitor_policy, Indices_of_monitors_intersecting_RectSpanningBothMonitors) {
    auto monitors = Two_monitors_same_dpi();
    RectPx r = RectPx::From_ltrb(1000, 100, 2500, 500);
    auto indices = Indices_of_monitors_intersecting(r, monitors);
    EXPECT_EQ(indices.size(), 2u);
    // Order is implementation-defined; both 0 and 1 must appear.
    bool has0 = (indices[0] == 0u || indices[1] == 0u);
    bool has1 = (indices[0] == 1u || indices[1] == 1u);
    EXPECT_TRUE(has0);
    EXPECT_TRUE(has1);
}

TEST(monitor_policy,
     Allowed_selection_rect_SameDPI_SameOrientation_CandidateSpanningBoth) {
    auto monitors = Two_monitors_same_dpi();
    PointPx start{100, 100};
    RectPx candidate = RectPx::From_ltrb(100, 100, 2500, 500);
    RectPx allowed = Allowed_selection_rect(candidate, start, monitors);
    EXPECT_EQ(allowed.left, candidate.left);
    EXPECT_EQ(allowed.top, candidate.top);
    EXPECT_EQ(allowed.right, candidate.right);
    EXPECT_EQ(allowed.bottom, candidate.bottom);
}

TEST(monitor_policy,
     Allowed_selection_rect_DifferentDPI_CandidateSpanningBoth_ClampedToStart) {
    auto monitors = Two_monitors_different_dpi();
    PointPx start{100, 100};                                   // in first monitor
    RectPx candidate = RectPx::From_ltrb(100, 100, 2500, 500); // spans both
    RectPx allowed = Allowed_selection_rect(candidate, start, monitors);
    // Clamped to first monitor [0,0,1920,1080]
    EXPECT_EQ(allowed.left, 100);
    EXPECT_EQ(allowed.top, 100);
    EXPECT_EQ(allowed.right, 1920);
    EXPECT_EQ(allowed.bottom, 500);
}

TEST(monitor_policy, Allowed_selection_rect_SingleMonitor_CandidateInside_Unchanged) {
    auto monitors = Two_monitors_same_dpi();
    PointPx start{500, 500};
    RectPx candidate = RectPx::From_ltrb(100, 100, 800, 600);
    RectPx allowed = Allowed_selection_rect(candidate, start, monitors);
    EXPECT_EQ(allowed.left, 100);
    EXPECT_EQ(allowed.top, 100);
    EXPECT_EQ(allowed.right, 800);
    EXPECT_EQ(allowed.bottom, 600);
}

TEST(monitor_policy,
     Allowed_selection_rect_StartOutsideAllMonitors_ClampedToFirstTouched) {
    auto monitors = Two_monitors_different_dpi();
    PointPx start{-100, -100};                                 // outside all
    RectPx candidate = RectPx::From_ltrb(100, 100, 2500, 500); // spans both
    RectPx allowed = Allowed_selection_rect(candidate, start, monitors);
    // Cross-monitor not allowed; start outside → clamp to first touched (monitor
    // 0)
    EXPECT_EQ(allowed.left, 100);
    EXPECT_EQ(allowed.top, 100);
    EXPECT_EQ(allowed.right, 1920);
    EXPECT_EQ(allowed.bottom, 500);
}

TEST(monitor_policy, Allowed_selection_rect_EmptyCandidate_ReturnedAsIs) {
    auto monitors = Two_monitors_same_dpi();
    PointPx start{100, 100};
    RectPx candidate =
        RectPx::From_points(PointPx{50, 50}, PointPx{50, 50}); // zero size
    RectPx allowed = Allowed_selection_rect(candidate, start, monitors);
    EXPECT_TRUE(allowed.Is_empty());
    EXPECT_EQ(allowed.left, 50);
    EXPECT_EQ(allowed.right, 50);
    EXPECT_EQ(allowed.top, 50);
    EXPECT_EQ(allowed.bottom, 50);
}

TEST(monitor_policy, Allowed_selection_rect_NoMonitors_CandidateReturned) {
    std::vector<MonitorWithBounds> empty;
    RectPx candidate = RectPx::From_ltrb(10, 20, 100, 200);
    RectPx allowed = Allowed_selection_rect(candidate, PointPx{50, 50}, empty);
    EXPECT_EQ(allowed.left, 10);
    EXPECT_EQ(allowed.top, 20);
    EXPECT_EQ(allowed.right, 100);
    EXPECT_EQ(allowed.bottom, 200);
}

TEST(monitor_policy,
     Allowed_selection_rect_DifferentOrientation_ClampedToStartMonitor) {
    auto monitors = Two_monitors_different_orientation();
    PointPx start{100, 100}; // first (landscape)
    RectPx candidate =
        RectPx::From_ltrb(100, 100, 500, 1500); // would span into portrait
    RectPx allowed = Allowed_selection_rect(candidate, start, monitors);
    // First monitor bounds: [0,0,1920,1080]
    EXPECT_EQ(allowed.left, 100);
    EXPECT_EQ(allowed.top, 100);
    EXPECT_EQ(allowed.right, 500);
    EXPECT_EQ(allowed.bottom, 1080);
}

// Jocelyn's desk: laptop left of the primary and lower, two panels at the top.
TEST(monitor_policy, Monitor_rects_in_client_NegativeOriginDesk) {
    MonitorInfo const info{};
    std::vector<MonitorWithBounds> const monitors = {
        {RectPx::From_ltrb(-1920, 137, 0, 1337), info},
        {RectPx::From_ltrb(0, 0, 2560, 1440), info},
        {RectPx::From_ltrb(2560, 0, 5120, 1440), info},
    };
    std::vector<RectPx> const rects =
        Monitor_rects_in_client(monitors, PointPx{-1920, 0});
    ASSERT_EQ(rects.size(), 3u);
    EXPECT_EQ(rects[0], RectPx::From_ltrb(0, 137, 1920, 1337));
    EXPECT_EQ(rects[1], RectPx::From_ltrb(1920, 0, 4480, 1440));
    EXPECT_EQ(rects[2], RectPx::From_ltrb(4480, 0, 7040, 1440));
}

// A monitor above the primary puts the origin at negative y.
TEST(monitor_policy, Monitor_rects_in_client_NegativeY) {
    MonitorInfo const info{};
    std::vector<MonitorWithBounds> const monitors = {
        {RectPx::From_ltrb(0, 0, 1920, 1080), info},
        {RectPx::From_ltrb(0, -1440, 2560, 0), info},
    };
    std::vector<RectPx> const rects =
        Monitor_rects_in_client(monitors, PointPx{0, -1440});
    ASSERT_EQ(rects.size(), 2u);
    EXPECT_EQ(rects[0], RectPx::From_ltrb(0, 1440, 1920, 2520));
    EXPECT_EQ(rects[1], RectPx::From_ltrb(0, 0, 2560, 1440));
}

TEST(monitor_policy, Monitor_rects_in_client_NoMonitors) {
    EXPECT_TRUE(Monitor_rects_in_client({}, PointPx{5, 5}).empty());
}
