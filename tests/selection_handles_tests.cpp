#include "greenflame_core/rect_px.h"
#include "greenflame_core/selection_handles.h"

using namespace greenflame::core;

// ---------------------------------------------------------------------------
// Hit_test_border_zone tests
// Selection [100, 50, 200, 150]: 100x100 px, corner_w=corner_h=16, band=5
// ---------------------------------------------------------------------------

TEST(selection_handles, Hit_test_border_zone_NulloptForEmpty) {
    RectPx empty = RectPx::From_ltrb(10, 10, 10, 10);
    EXPECT_EQ(Hit_test_border_zone(empty, PointPx{10, 10}), std::nullopt);
}

TEST(selection_handles, Hit_test_border_zone_NulloptFarInside) {
    RectPx sel = RectPx::From_ltrb(100, 50, 200, 150);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 100}), std::nullopt);
}

TEST(selection_handles, Hit_test_border_zone_NulloptFarOutside) {
    RectPx sel = RectPx::From_ltrb(100, 50, 200, 150);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{0, 0}), std::nullopt);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{300, 200}), std::nullopt);
}

TEST(selection_handles, Hit_test_border_zone_TopEdgeCenter) {
    RectPx sel = RectPx::From_ltrb(100, 50, 200, 150);
    // Center of top edge (x=150), on the edge, inside band, outside band
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 50}), SelectionHandle::Top);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 52}), SelectionHandle::Top);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 55}), SelectionHandle::Top);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 48}), SelectionHandle::Top);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 45}), SelectionHandle::Top);
}

TEST(selection_handles, Hit_test_border_zone_TopEdgePastBand) {
    RectPx sel = RectPx::From_ltrb(100, 50, 200, 150);
    // 6 px inside or outside the top edge → outside the 5px band
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 56}), std::nullopt);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 44}), std::nullopt);
}

TEST(selection_handles, Hit_test_border_zone_BottomEdgeCenter) {
    RectPx sel = RectPx::From_ltrb(100, 50, 200, 150);
    // Bottom border pixel is at y=149
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 149}), SelectionHandle::Bottom);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 150}), SelectionHandle::Bottom);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 154}), SelectionHandle::Bottom);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 147}), SelectionHandle::Bottom);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{150, 144}), SelectionHandle::Bottom);
}

TEST(selection_handles, Hit_test_border_zone_LeftEdgeCenter) {
    RectPx sel = RectPx::From_ltrb(100, 50, 200, 150);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{100, 100}), SelectionHandle::Left);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{103, 100}), SelectionHandle::Left);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{105, 100}), SelectionHandle::Left);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{97, 100}), SelectionHandle::Left);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{95, 100}), SelectionHandle::Left);
}

TEST(selection_handles, Hit_test_border_zone_RightEdgeCenter) {
    RectPx sel = RectPx::From_ltrb(100, 50, 200, 150);
    // Right border pixel is at x=199
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{199, 100}), SelectionHandle::Right);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{200, 100}), SelectionHandle::Right);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{204, 100}), SelectionHandle::Right);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{197, 100}), SelectionHandle::Right);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{194, 100}), SelectionHandle::Right);
}

TEST(selection_handles, Hit_test_border_zone_Corners) {
    RectPx sel = RectPx::From_ltrb(100, 50, 200, 150);

    // TopLeft: approached from top border
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{100, 50}), SelectionHandle::TopLeft);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{108, 50}), SelectionHandle::TopLeft);
    // TopLeft: approached from left border
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{100, 58}), SelectionHandle::TopLeft);

    // TopRight: approached from top border
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{199, 50}), SelectionHandle::TopRight);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{190, 50}), SelectionHandle::TopRight);
    // TopRight: approached from right border
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{199, 58}), SelectionHandle::TopRight);

    // BottomRight: approached from bottom border
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{199, 149}), SelectionHandle::BottomRight);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{190, 149}), SelectionHandle::BottomRight);
    // BottomRight: approached from right border
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{199, 140}), SelectionHandle::BottomRight);

    // BottomLeft: approached from bottom border
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{100, 149}), SelectionHandle::BottomLeft);
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{108, 149}), SelectionHandle::BottomLeft);
    // BottomLeft: approached from left border
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{100, 140}), SelectionHandle::BottomLeft);
}

TEST(selection_handles, Hit_test_border_zone_TransitionPointCornerToEdge) {
    RectPx sel = RectPx::From_ltrb(100, 50, 200, 150);
    // corner_w = 16; transition at x = 100 + 16 = 116
    // x=115 on top band → in_lc (115 < 116) → TopLeft
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{115, 50}), SelectionHandle::TopLeft);
    // x=116 on top band → NOT in_lc (116 >= 116), NOT in_rc → Top
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{116, 50}), SelectionHandle::Top);
    // x=183 on top band → NOT in_rc (183 < 184) → Top
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{183, 50}), SelectionHandle::Top);
    // x=184 on top band → in_rc (184 >= 200-16=184) → TopRight
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{184, 50}), SelectionHandle::TopRight);
}

TEST(selection_handles, Hit_test_border_zone_DegenerateNarrowRect) {
    // 20px-wide rect: corner_w = min(16, 10) = 10; no Top edge zone
    RectPx sel = RectPx::From_ltrb(100, 50, 120, 150);
    // x=110 (midpoint): in_rc (110 >= 120-10=110) → TopRight, not Top
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{110, 50}), SelectionHandle::TopRight);
    // x=109: in_lc (109 < 110) → TopLeft
    EXPECT_EQ(Hit_test_border_zone(sel, PointPx{109, 50}), SelectionHandle::TopLeft);
    // No pure Top should be returned for any x in [100,120)
    for (int x = 100; x < 120; ++x) {
        auto h = Hit_test_border_zone(sel, PointPx{x, 50});
        EXPECT_NE(h, SelectionHandle::Top) << "x=" << x;
    }
}

TEST(selection_handles, Resize_rect_from_handle_TopLeft_MovesToCursor) {
    RectPx anchor = RectPx::From_ltrb(100, 50, 200, 150);
    PointPx cursor = {80, 40};
    RectPx out = Resize_rect_from_handle(anchor, SelectionHandle::TopLeft, cursor);
    EXPECT_EQ(out.left, 80);
    EXPECT_EQ(out.top, 40);
    EXPECT_EQ(out.right, 200);
    EXPECT_EQ(out.bottom, 150);
}

TEST(selection_handles, Resize_rect_from_handle_BottomRight_MovesToCursor) {
    RectPx anchor = RectPx::From_ltrb(100, 50, 200, 150);
    PointPx cursor = {220, 160};
    RectPx out = Resize_rect_from_handle(anchor, SelectionHandle::BottomRight, cursor);
    EXPECT_EQ(out.left, 100);
    EXPECT_EQ(out.top, 50);
    EXPECT_EQ(out.right, 220);
    EXPECT_EQ(out.bottom, 160);
}

TEST(selection_handles, Resize_rect_from_handle_Top_MovesOnlyTop) {
    RectPx anchor = RectPx::From_ltrb(100, 50, 200, 150);
    RectPx out =
        Resize_rect_from_handle(anchor, SelectionHandle::Top, PointPx{150, 30});
    EXPECT_EQ(out.left, 100);
    EXPECT_EQ(out.top, 30);
    EXPECT_EQ(out.right, 200);
    EXPECT_EQ(out.bottom, 150);
}

TEST(selection_handles, Resize_rect_from_handle_EnforcesMinimumSize) {
    RectPx anchor = RectPx::From_ltrb(100, 50, 200, 150);
    PointPx cursor = {250, 50}; // drag TopRight far right, same top -> width 150
    RectPx out = Resize_rect_from_handle(anchor, SelectionHandle::TopRight, cursor);
    EXPECT_GE(out.Width(), 1);
    EXPECT_GE(out.Height(), 1);
}

TEST(selection_handles, Anchor_point_for_resize_policy_OppositeCorner) {
    RectPx r = RectPx::From_ltrb(100, 50, 200, 150);
    PointPx p;
    p = Anchor_point_for_resize_policy(r, SelectionHandle::TopLeft);
    EXPECT_EQ(p.x, 200);
    EXPECT_EQ(p.y, 150);
    p = Anchor_point_for_resize_policy(r, SelectionHandle::BottomRight);
    EXPECT_EQ(p.x, 100);
    EXPECT_EQ(p.y, 50);
    p = Anchor_point_for_resize_policy(r, SelectionHandle::TopRight);
    EXPECT_EQ(p.x, 100);
    EXPECT_EQ(p.y, 150);
    p = Anchor_point_for_resize_policy(r, SelectionHandle::BottomLeft);
    EXPECT_EQ(p.x, 200);
    EXPECT_EQ(p.y, 50);
}

TEST(selection_handles, Anchor_point_for_resize_policy_FixedEdgeCenter) {
    RectPx r = RectPx::From_ltrb(100, 50, 200, 150);
    PointPx p;
    p = Anchor_point_for_resize_policy(r, SelectionHandle::Top);
    EXPECT_EQ(p.x, 150);
    EXPECT_EQ(p.y, 150);
    p = Anchor_point_for_resize_policy(r, SelectionHandle::Bottom);
    EXPECT_EQ(p.x, 150);
    EXPECT_EQ(p.y, 50);
    p = Anchor_point_for_resize_policy(r, SelectionHandle::Left);
    EXPECT_EQ(p.x, 200);
    EXPECT_EQ(p.y, 100);
    p = Anchor_point_for_resize_policy(r, SelectionHandle::Right);
    EXPECT_EQ(p.x, 100);
    EXPECT_EQ(p.y, 100);
}
