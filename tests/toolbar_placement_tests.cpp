#include "greenflame_core/toolbar_placement.h"

using namespace greenflame::core;

namespace {

// 1920x1080 single monitor at origin.
RectPx const kFullHd = RectPx::From_ltrb(0, 0, 1920, 1080);

// Helper to build params with the standard icon dimensions.
ToolbarPlacementParams Make_params(RectPx selection, std::span<const RectPx> available,
                                   int count = 20) {
    ToolbarPlacementParams p{};
    p.selection = selection;
    p.available = available;
    p.button_size = 36;
    p.separator = 9;
    p.button_count = count;
    return p;
}

} // namespace

// Zero button_count → empty result.
TEST(ToolbarPlacement, ZeroButtonCount) {
    RectPx const sel = RectPx::From_ltrb(100, 100, 500, 400);
    RectPx const avail[] = {kFullHd};
    auto const r = Compute_toolbar_placement(Make_params(sel, avail, 0));
    EXPECT_TRUE(r.positions.empty());
    EXPECT_FALSE(r.buttons_inside);
}

// Empty available span → does not crash, falls back to inside placement.
TEST(ToolbarPlacement, EmptyAvailableDoesNotCrash) {
    RectPx const sel = RectPx::From_ltrb(100, 100, 500, 400);
    auto const r = Compute_toolbar_placement(Make_params(sel, {}, 20));
    EXPECT_EQ(static_cast<int>(r.positions.size()), 20);
    EXPECT_TRUE(r.buttons_inside);
}

// Wide selection with room below → first row of buttons placed at the bottom.
TEST(ToolbarPlacement, WideSelectionRoomBelow) {
    // Selection in the centre of the screen — plenty of room on all sides.
    RectPx const sel = RectPx::From_ltrb(200, 300, 1200, 600);
    RectPx const avail[] = {kFullHd};
    auto const r = Compute_toolbar_placement(Make_params(sel, avail, 20));
    ASSERT_FALSE(r.positions.empty());
    EXPECT_FALSE(r.buttons_inside);
    // First button should be below the selection (y > sel.bottom - 1).
    EXPECT_GT(r.positions[0].y, sel.bottom - 1);
}

// Tall selection with room to the right → first button placed to the right
// (after the bottom row which should be empty or just have a few).
TEST(ToolbarPlacement, TallSelectionRoomRight) {
    // Narrow selection near the left edge — right side is open.
    RectPx const sel = RectPx::From_ltrb(10, 50, 60, 900);
    RectPx const avail[] = {kFullHd};
    auto const r = Compute_toolbar_placement(Make_params(sel, avail, 5));
    ASSERT_EQ(static_cast<int>(r.positions.size()), 5);
    EXPECT_FALSE(r.buttons_inside);
    // At least one button should be to the right of the selection.
    bool any_right = false;
    for (auto const &pos : r.positions) {
        if (pos.x >= sel.right) {
            any_right = true;
            break;
        }
    }
    EXPECT_TRUE(any_right);
}

// Small selection near screen centre — 20 buttons wrap across multiple sides.
TEST(ToolbarPlacement, SmallSelectionAllSidesOpen) {
    RectPx const sel = RectPx::From_ltrb(900, 500, 950, 550);
    RectPx const avail[] = {kFullHd};
    auto const r = Compute_toolbar_placement(Make_params(sel, avail, 20));
    EXPECT_EQ(static_cast<int>(r.positions.size()), 20);
    EXPECT_FALSE(r.buttons_inside);
}

// All sides blocked → buttons_inside = true, positions within selection.
TEST(ToolbarPlacement, AllSidesBlockedButtonsInside) {
    // Available region is exactly the selection — no room outside.
    RectPx const sel = RectPx::From_ltrb(200, 200, 800, 600);
    RectPx const avail[] = {sel};
    auto const r = Compute_toolbar_placement(Make_params(sel, avail, 20));
    EXPECT_TRUE(r.buttons_inside);
    EXPECT_EQ(static_cast<int>(r.positions.size()), 20);
}

// Min-size enforcement: 1×1 selection → valid non-empty positions, no crash.
TEST(ToolbarPlacement, MinSizeEnforcementTinySelection) {
    RectPx const sel = RectPx::From_ltrb(500, 400, 501, 401);
    RectPx const avail[] = {kFullHd};
    auto const r = Compute_toolbar_placement(Make_params(sel, avail, 20));
    EXPECT_EQ(static_cast<int>(r.positions.size()), 20);
}

// Selection near bottom-right corner: bottom and right sides blocked.
TEST(ToolbarPlacement, SelectionAtBottomRightCorner) {
    // Selection touching the screen's bottom-right — only top and left open.
    RectPx const sel = RectPx::From_ltrb(1700, 900, 1920, 1080);
    RectPx const avail[] = {kFullHd};
    auto const r = Compute_toolbar_placement(Make_params(sel, avail, 10));
    EXPECT_EQ(static_cast<int>(r.positions.size()), 10);
    EXPECT_FALSE(r.buttons_inside);
    // No button should be placed off the bottom edge.
    for (auto const &pos : r.positions) {
        EXPECT_LT(pos.y, kFullHd.bottom) << "Button y=" << pos.y << " is off screen";
    }
}

// Selection spans the full screen width: left and right sides blocked.
TEST(ToolbarPlacement, FullWidthSelectionBothHorizontalBlocked) {
    RectPx const sel = RectPx::From_ltrb(0, 400, 1920, 600);
    RectPx const avail[] = {kFullHd};
    auto const r = Compute_toolbar_placement(Make_params(sel, avail, 10));
    EXPECT_EQ(static_cast<int>(r.positions.size()), 10);
    EXPECT_FALSE(r.buttons_inside);
}

// Single button placed correctly (no off-by-one crash).
TEST(ToolbarPlacement, SingleButton) {
    RectPx const sel = RectPx::From_ltrb(400, 300, 800, 600);
    RectPx const avail[] = {kFullHd};
    auto const r = Compute_toolbar_placement(Make_params(sel, avail, 1));
    ASSERT_EQ(static_cast<int>(r.positions.size()), 1);
    EXPECT_FALSE(r.buttons_inside);
}
