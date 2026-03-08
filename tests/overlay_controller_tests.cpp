#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/overlay_controller.h"
#include "greenflame_core/snap_edge_builder.h"

using namespace greenflame::core;

// ---------------------------------------------------------------------------
// Fixture helpers
// ---------------------------------------------------------------------------

namespace {

MonitorWithBounds Make_monitor(int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t scale_pct = 100) {
    MonitorWithBounds m;
    m.bounds = RectPx::From_ltrb(x, y, x + w, y + h);
    m.info.dpi_scale.percent = scale_pct;
    m.info.orientation = MonitorOrientation::Landscape;
    return m;
}

std::vector<MonitorWithBounds> Single_monitor() {
    return {Make_monitor(0, 0, 1920, 1080)};
}

std::vector<MonitorWithBounds> Dual_same_dpi() {
    return {Make_monitor(0, 0, 1920, 1080), Make_monitor(1920, 0, 1920, 1080)};
}

std::vector<MonitorWithBounds> Dual_diff_dpi() {
    return {Make_monitor(0, 0, 1920, 1080, 100),
            Make_monitor(1920, 0, 1920, 1080, 150)};
}

OverlayController
Make_controller(std::vector<MonitorWithBounds> monitors = Single_monitor()) {
    OverlayController c;
    c.Reset_for_session(std::move(monitors));
    return c;
}

OverlayModifierState No_mods() { return {}; }
OverlayModifierState Shift_only() { return {true, false, false}; }
OverlayModifierState Ctrl_only() { return {false, true, false}; }
OverlayModifierState Shift_ctrl() { return {true, true, false}; }
OverlayModifierState Alt_only() { return {false, false, true}; }

// Convenience: build vis_rects already including the monitor bounds (as Win32 layer
// would).
std::vector<RectPx> Make_vis_rects(OverlayController const &c,
                                   std::vector<RectPx> window_rects = {}) {
    for (auto const &m : c.State().cached_monitors) {
        window_rects.push_back(m.bounds);
    }
    return window_rects;
}

// Shorthand for a fresh-drag press with no snap-relevant rects.
OverlayAction Press(OverlayController &c, PointPx pt,
                    OverlayModifierState mods = No_mods(), int32_t ox = 0,
                    int32_t oy = 0) {
    return c.On_primary_press(mods, pt, pt, std::nullopt, std::nullopt, std::nullopt,
                              {}, Make_vis_rects(c), ox, oy);
}

OverlayAction Move(OverlayController &c, PointPx pt,
                   OverlayModifierState mods = No_mods(), uint64_t now_ms = 100) {
    return c.On_pointer_move(mods, pt, pt, std::nullopt, {}, std::nullopt, 0, 0,
                             now_ms);
}

OverlayAction Release(OverlayController &c, PointPx pt,
                      OverlayModifierState mods = No_mods()) {
    return c.On_primary_release(mods, pt);
}

} // namespace

// ===========================================================================
// Group A — Initial State
// ===========================================================================

TEST(overlay_controller, A_InitialState_AllDragsFalse) {
    auto c = Make_controller();
    auto const &s = c.State();
    EXPECT_FALSE(s.dragging);
    EXPECT_FALSE(s.handle_dragging);
    EXPECT_FALSE(s.move_dragging);
    EXPECT_FALSE(s.modifier_preview);
}

TEST(overlay_controller, A_InitialState_RectsEmpty) {
    auto c = Make_controller();
    auto const &s = c.State();
    EXPECT_TRUE(s.live_rect.Is_empty());
    EXPECT_TRUE(s.final_selection.Is_empty());
}

TEST(overlay_controller, A_InitialState_SourceRegion) {
    auto c = Make_controller();
    EXPECT_EQ(c.State().selection_source, SaveSelectionSource::Region);
}

TEST(overlay_controller, A_InitialState_NoWindow) {
    auto c = Make_controller();
    EXPECT_FALSE(c.State().selection_window.has_value());
}

TEST(overlay_controller, A_InitialState_CachedMonitors) {
    auto monitors = Single_monitor();
    OverlayController c;
    c.Reset_for_session(monitors);
    EXPECT_EQ(c.State().cached_monitors.size(), 1u);
    EXPECT_EQ(c.State().cached_monitors[0].bounds,
              (RectPx::From_ltrb(0, 0, 1920, 1080)));
}

// ===========================================================================
// Group B — Fresh Drag
// ===========================================================================

TEST(overlay_controller, B_Press_SetsDraggingAndReturnsRepaint) {
    auto c = Make_controller();
    OverlayAction action = Press(c, {100, 200});
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_TRUE(c.State().dragging);
    EXPECT_EQ(c.State().start_px, (PointPx{100, 200}));
}

TEST(overlay_controller, B_Move_UpdatesLiveRect) {
    auto c = Make_controller();
    Press(c, {100, 200});
    Move(c, {300, 400});
    auto const &s = c.State();
    EXPECT_EQ(s.live_rect.left, 100);
    EXPECT_EQ(s.live_rect.top, 200);
    EXPECT_EQ(s.live_rect.right, 300);
    EXPECT_EQ(s.live_rect.bottom, 400);
}

TEST(overlay_controller, B_Release_CommitsFinalSelection) {
    auto c = Make_controller();
    Press(c, {100, 200});
    Move(c, {300, 400});
    OverlayAction action = Release(c, {300, 400});
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().dragging);
    EXPECT_EQ(c.State().final_selection, (RectPx::From_ltrb(100, 200, 300, 400)));
    EXPECT_EQ(c.State().selection_source, SaveSelectionSource::Region);
}

TEST(overlay_controller, B_Release_SelectionWindowIsNullopt) {
    auto c = Make_controller();
    Press(c, {100, 200});
    Release(c, {300, 400});
    EXPECT_FALSE(c.State().selection_window.has_value());
    EXPECT_FALSE(c.State().selection_monitor_index.has_value());
}

TEST(overlay_controller, B_SnapOn_NearEdgeSnaps) {
    auto c = Make_controller();
    // Window rect at screen coords 200,200..400,400, overlay origin at 0,0
    std::vector<RectPx> win_rects = {RectPx::From_ltrb(200, 200, 400, 400)};
    for (auto const &m : c.State().cached_monitors) {
        win_rects.push_back(m.bounds);
    }
    // Press near left edge 200 (cursor at 207 — within threshold 10)
    std::ignore = c.On_primary_press(No_mods(), {207, 100}, {207, 100}, std::nullopt,
                                     std::nullopt, std::nullopt, {}, win_rects, 0, 0);
    EXPECT_EQ(c.State().start_px.x, 200); // snapped to edge
}

TEST(overlay_controller, B_SnapOff_AltHeld_NearEdgeDoesNotSnap) {
    auto c = Make_controller();
    std::vector<RectPx> win_rects = {RectPx::From_ltrb(200, 200, 400, 400)};
    for (auto const &m : c.State().cached_monitors) {
        win_rects.push_back(m.bounds);
    }
    std::ignore = c.On_primary_press(Alt_only(), {207, 100}, {207, 100}, std::nullopt,
                                     std::nullopt, std::nullopt, {}, win_rects, 0, 0);
    EXPECT_EQ(c.State().start_px.x, 207); // not snapped
}

TEST(overlay_controller, B_DualSameDpi_SelectionAllowed) {
    auto c = Make_controller(Dual_same_dpi());
    Press(c, {100, 100});
    Release(c, {2000, 800}); // spans both monitors
    EXPECT_FALSE(c.State().final_selection.Is_empty());
    EXPECT_GT(c.State().final_selection.Width(), 1500); // not clamped
}

TEST(overlay_controller, B_DualDiffDpi_SelectionClampedToStartMonitor) {
    auto c = Make_controller(Dual_diff_dpi());
    Press(c, {100, 100});    // start on monitor 0 (100 DPI)
    Release(c, {2100, 800}); // end on monitor 1 (150 DPI) — different DPI, clamped
    auto const &sel = c.State().final_selection;
    EXPECT_FALSE(sel.Is_empty());
    EXPECT_LE(sel.right, 1920); // clamped to monitor 0
}

// ===========================================================================
// Group C — Modifier Preview
// ===========================================================================

TEST(overlay_controller, C_Ctrl_SetsModifierPreview) {
    auto c = Make_controller();
    RectPx win_screen = RectPx::From_ltrb(100, 50, 400, 300);
    OverlayAction action = c.On_modifier_changed(Ctrl_only(), {200, 150}, win_screen,
                                                 {}, std::nullopt, 0, 0);
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_TRUE(c.State().modifier_preview);
    // live_rect should be win_screen offset to client coords (origin 0,0 → same)
    EXPECT_EQ(c.State().live_rect, win_screen);
}

TEST(overlay_controller, C_Shift_SetsModifierPreview_MonitorBounds) {
    auto c = Make_controller(); // 1920x1080 monitor at 0,0
    OverlayAction action = c.On_modifier_changed(Shift_only(), {500, 300}, {}, {},
                                                 std::optional<size_t>{0u}, 0, 0);
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_TRUE(c.State().modifier_preview);
    EXPECT_EQ(c.State().live_rect, (RectPx::From_ltrb(0, 0, 1920, 1080)));
}

TEST(overlay_controller, C_ShiftCtrl_SetsDesktopPreview) {
    auto c = Make_controller(Dual_same_dpi());
    RectPx vdesk = RectPx::From_ltrb(0, 0, 3840, 1080);
    OverlayAction action =
        c.On_modifier_changed(Shift_ctrl(), {1000, 500}, {}, vdesk, std::nullopt, 0, 0);
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_TRUE(c.State().modifier_preview);
    EXPECT_EQ(c.State().live_rect, vdesk);
}

TEST(overlay_controller, C_ShiftReleased_ClearsModifierPreview) {
    auto c = Make_controller();
    // Establish shift preview
    std::ignore = c.On_modifier_changed(Shift_only(), {500, 300}, {}, {},
                                        std::optional<size_t>{0u}, 0, 0);
    ASSERT_TRUE(c.State().modifier_preview);
    // Release shift (new_mods has no shift or ctrl)
    OverlayAction action = c.On_modifier_changed(No_mods(), {}, {}, {}, {}, 0, 0);
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().modifier_preview);
    EXPECT_TRUE(c.State().live_rect.Is_empty());
}

TEST(overlay_controller, C_NoPreviewWhileDragging) {
    auto c = Make_controller();
    Press(c, {100, 100});
    ASSERT_TRUE(c.State().dragging);
    // Shift key-down while dragging — should not set preview
    std::ignore = c.On_modifier_changed(Shift_only(), {500, 300}, {}, {},
                                        std::optional<size_t>{0u}, 0, 0);
    EXPECT_FALSE(c.State().modifier_preview);
}

TEST(overlay_controller, C_NoPreviewWhileHandleDragging) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300}); // commit selection
    // Start handle drag at TopLeft corner
    std::ignore =
        c.On_primary_press(No_mods(), {100, 100}, {100, 100}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);
    ASSERT_TRUE(c.State().handle_dragging);
    std::ignore = c.On_modifier_changed(Shift_only(), {500, 300}, {}, {},
                                        std::optional<size_t>{0u}, 0, 0);
    EXPECT_FALSE(c.State().modifier_preview);
}

TEST(overlay_controller, C_NoPreviewWhenFinalSelectionNonEmpty) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    ASSERT_FALSE(c.State().final_selection.Is_empty());
    std::ignore = c.On_modifier_changed(
        Ctrl_only(), {200, 200},
        std::optional<RectPx>{RectPx::From_ltrb(50, 50, 200, 200)}, {}, std::nullopt, 0,
        0);
    EXPECT_FALSE(c.State().modifier_preview);
}

// ===========================================================================
// Group D — Commit During Preview
// ===========================================================================

TEST(overlay_controller, D_CtrlPreview_PressCommitsWindow) {
    auto c = Make_controller();
    RectPx win_screen = RectPx::From_ltrb(100, 50, 400, 300);
    std::ignore = c.On_modifier_changed(Ctrl_only(), {200, 150}, win_screen, {},
                                        std::nullopt, 0, 0);
    ASSERT_TRUE(c.State().modifier_preview);

    HWND fake_hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0xABCD));
    OverlayAction action = c.On_primary_press(
        Ctrl_only(), {200, 150}, {200, 150}, std::optional<HWND>{fake_hwnd},
        std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);

    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().modifier_preview);
    EXPECT_EQ(c.State().final_selection, win_screen);
    EXPECT_EQ(c.State().selection_source, SaveSelectionSource::Window);
    EXPECT_EQ(c.State().selection_window, std::optional<HWND>{fake_hwnd});
    EXPECT_FALSE(c.State().dragging);
}

TEST(overlay_controller, D_ShiftPreview_PressCommitsMonitor) {
    auto c = Make_controller();
    std::ignore = c.On_modifier_changed(Shift_only(), {500, 300}, {}, {},
                                        std::optional<size_t>{0u}, 0, 0);
    ASSERT_TRUE(c.State().modifier_preview);

    OverlayAction action = c.On_primary_press(
        Shift_only(), {500, 300}, {500, 300}, std::nullopt, std::optional<size_t>{0u},
        std::nullopt, {}, Make_vis_rects(c), 0, 0);

    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_EQ(c.State().selection_source, SaveSelectionSource::Monitor);
    EXPECT_EQ(c.State().selection_monitor_index, std::optional<size_t>{0u});
    EXPECT_FALSE(c.State().dragging);
}

TEST(overlay_controller, D_ShiftCtrlPreview_PressCommitsDesktop) {
    auto c = Make_controller(Dual_same_dpi());
    RectPx vdesk = RectPx::From_ltrb(0, 0, 3840, 1080);
    std::ignore =
        c.On_modifier_changed(Shift_ctrl(), {1000, 500}, {}, vdesk, std::nullopt, 0, 0);
    ASSERT_TRUE(c.State().modifier_preview);

    OverlayAction action =
        c.On_primary_press(Shift_ctrl(), {1000, 500}, {1000, 500}, std::nullopt,
                           std::nullopt, std::nullopt, vdesk, Make_vis_rects(c), 0, 0);

    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_EQ(c.State().selection_source, SaveSelectionSource::Desktop);
    EXPECT_FALSE(c.State().dragging);
}

// ===========================================================================
// Group E — Handle Drag
// ===========================================================================

TEST(overlay_controller, E_PressNearHandle_StartsHandleDrag) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    ASSERT_FALSE(c.State().final_selection.Is_empty());

    // Press exactly on TopLeft handle (100,100)
    OverlayAction action =
        c.On_primary_press(No_mods(), {100, 100}, {100, 100}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);

    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_TRUE(c.State().handle_dragging);
    EXPECT_EQ(c.State().resize_handle,
              std::optional<SelectionHandle>{SelectionHandle::TopLeft});
}

TEST(overlay_controller, E_HandleMove_UpdatesLiveRect) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    std::ignore =
        c.On_primary_press(No_mods(), {100, 100}, {100, 100}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);
    ASSERT_TRUE(c.State().handle_dragging);

    Move(c, {50, 50});
    EXPECT_FALSE(c.State().live_rect.Is_empty());
    EXPECT_EQ(c.State().live_rect.left, 50);
    EXPECT_EQ(c.State().live_rect.top, 50);
}

TEST(overlay_controller, E_HandleRelease_CommitsFinalSelection) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    std::ignore =
        c.On_primary_press(No_mods(), {100, 100}, {100, 100}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);
    Move(c, {50, 50});
    OverlayAction action = Release(c, {50, 50});
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().handle_dragging);
    EXPECT_FALSE(c.State().final_selection.Is_empty());
}

TEST(overlay_controller, E_CancelHandleDrag_FinalSelectionUnchanged) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    RectPx const original_sel = c.State().final_selection;

    std::ignore =
        c.On_primary_press(No_mods(), {100, 100}, {100, 100}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);
    Move(c, {50, 50});
    OverlayAction action = c.On_cancel();
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().handle_dragging);
    EXPECT_TRUE(c.State().live_rect.Is_empty());
    EXPECT_EQ(c.State().final_selection, original_sel); // unchanged
}

// ===========================================================================
// Group F — Move Drag
// ===========================================================================

TEST(overlay_controller, F_ClickInsideSelection_StartsMoveDragInDefaultMode) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});

    OverlayAction action =
        c.On_primary_press(No_mods(), {200, 200}, {200, 200}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);

    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_TRUE(c.State().move_dragging);
}

TEST(overlay_controller, F_MoveDoc_MovesRect) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    std::ignore =
        c.On_primary_press(No_mods(), {200, 200}, {200, 200}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);

    Move(c, {250, 250}, No_mods(), 100);
    auto const &s = c.State();
    EXPECT_FALSE(s.live_rect.Is_empty());
    // grab offset = (200-100, 200-100) = (100,100); new cursor 250,250 → rect at
    // (150,150)
    EXPECT_EQ(s.live_rect.left, 150);
    EXPECT_EQ(s.live_rect.top, 150);
}

TEST(overlay_controller, F_MoveRelease_CommitsFinalSelection) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    std::ignore =
        c.On_primary_press(No_mods(), {200, 200}, {200, 200}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);
    Move(c, {250, 250}, No_mods(), 100);
    OverlayAction action = Release(c, {250, 250});
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().move_dragging);
    EXPECT_FALSE(c.State().final_selection.Is_empty());
}

TEST(overlay_controller, F_CancelMoveDrag_RestoresMoveAnchor) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    RectPx const original_sel = c.State().final_selection;

    std::ignore =
        c.On_primary_press(No_mods(), {200, 200}, {200, 200}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);
    Move(c, {400, 400}, No_mods(), 100);

    OverlayAction action = c.On_cancel();
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().move_dragging);
    EXPECT_EQ(c.State().final_selection, original_sel);
}

TEST(overlay_controller, F_ClickOutsideSelection_DoesNotStartFreshDrag) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});

    std::ignore =
        c.On_primary_press(No_mods(), {50, 50}, {50, 50}, std::nullopt, std::nullopt,
                           std::nullopt, {}, Make_vis_rects(c), 0, 0);
    // With annotation tools available outside the region, this no longer starts a
    // replacement capture drag.
    EXPECT_FALSE(c.State().dragging);
    EXPECT_FALSE(c.State().move_dragging);
    EXPECT_EQ(c.State().final_selection, (RectPx::From_ltrb(100, 100, 300, 300)));
}

TEST(overlay_controller,
     F_ClickOnAnnotation_StartsAnnotationDragInsteadOfSelectionMove) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});

    EXPECT_EQ(c.On_annotation_tool_hotkey(L'B'), OverlayAction::Repaint);
    std::ignore =
        c.On_primary_press(No_mods(), {120, 120}, {120, 120}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);
    std::ignore = c.On_pointer_move(No_mods(), {140, 140}, {140, 140}, std::nullopt, {},
                                    std::nullopt, 0, 0, 100u);
    std::ignore = c.On_primary_release(No_mods(), {140, 140});
    ASSERT_EQ(c.Annotations().size(), 1u);
    EXPECT_EQ(c.On_annotation_tool_hotkey(L'B'), OverlayAction::Repaint);
    ASSERT_EQ(c.Active_annotation_tool(), std::nullopt);

    OverlayAction action =
        c.On_primary_press(No_mods(), {130, 130}, {130, 130}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);

    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().move_dragging);
    EXPECT_TRUE(c.Is_annotation_dragging());
}

// ===========================================================================
// Group G — On_cancel Priority
// ===========================================================================

TEST(overlay_controller, G_Cancel_MoveDragging_Restores) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    std::ignore =
        c.On_primary_press(No_mods(), {200, 200}, {200, 200}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);
    ASSERT_TRUE(c.State().move_dragging);

    OverlayAction action = c.On_cancel();
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().move_dragging);
    EXPECT_FALSE(c.State().final_selection.Is_empty()); // restored, not closed
}

TEST(overlay_controller, G_Cancel_HandleDragging_ClearsDrag) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    std::ignore =
        c.On_primary_press(No_mods(), {100, 100}, {100, 100}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);
    ASSERT_TRUE(c.State().handle_dragging);

    OverlayAction action = c.On_cancel();
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().handle_dragging);
}

TEST(overlay_controller, G_Cancel_Dragging_ClearsDrag) {
    auto c = Make_controller();
    Press(c, {100, 100});
    ASSERT_TRUE(c.State().dragging);

    OverlayAction action = c.On_cancel();
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().dragging);
}

TEST(overlay_controller, G_Cancel_FinalSelection_ClearsSelection) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    ASSERT_FALSE(c.State().final_selection.Is_empty());

    OverlayAction action = c.On_cancel();
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_TRUE(c.State().final_selection.Is_empty());
}

TEST(overlay_controller, G_Cancel_FinalSelectionAlsoClearsAnnotations) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    ASSERT_FALSE(c.State().final_selection.Is_empty());

    EXPECT_EQ(c.On_annotation_tool_hotkey(L'B'), OverlayAction::Repaint);
    std::ignore =
        c.On_primary_press(No_mods(), {120, 120}, {120, 120}, std::nullopt,
                           std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0, 0);
    std::ignore = c.On_pointer_move(No_mods(), {140, 140}, {140, 140}, std::nullopt, {},
                                    std::nullopt, 0, 0, 100u);
    std::ignore = c.On_primary_release(No_mods(), {140, 140});
    ASSERT_EQ(c.Annotations().size(), 1u);

    OverlayAction action = c.On_cancel();
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_TRUE(c.State().final_selection.Is_empty());
    EXPECT_TRUE(c.Annotations().empty());
    EXPECT_EQ(c.Active_annotation_tool(), std::nullopt);
}

TEST(overlay_controller, G_Cancel_FinalSelectionResetsActiveToolForNextSelection) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    ASSERT_EQ(c.On_annotation_tool_hotkey(L'B'), OverlayAction::Repaint);
    ASSERT_EQ(c.Active_annotation_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Freehand});

    EXPECT_EQ(c.On_cancel(), OverlayAction::Repaint);
    EXPECT_TRUE(c.State().final_selection.Is_empty());
    EXPECT_EQ(c.Active_annotation_tool(), std::nullopt);

    EXPECT_EQ(Press(c, {400, 400}), OverlayAction::Repaint);
    EXPECT_EQ(Release(c, {600, 600}), OverlayAction::Repaint);
    EXPECT_EQ(c.Active_annotation_tool(), std::nullopt);
}

TEST(overlay_controller, G_Cancel_AllClear_ReturnsClose) {
    auto c = Make_controller();
    OverlayAction action = c.On_cancel();
    EXPECT_EQ(action, OverlayAction::Close);
}

// ===========================================================================
// Group H — Save / Copy
// ===========================================================================

TEST(overlay_controller, H_SaveDirect_WithSelection) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    EXPECT_EQ(c.On_save_requested(false, false), OverlayAction::SaveDirect);
}

TEST(overlay_controller, H_SaveDirectCopy_WithSelection) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    EXPECT_EQ(c.On_save_requested(false, true), OverlayAction::SaveDirectAndCopyFile);
}

TEST(overlay_controller, H_SaveAs_WithSelection) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    EXPECT_EQ(c.On_save_requested(true, false), OverlayAction::SaveAs);
}

TEST(overlay_controller, H_SaveAsCopy_WithSelection) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    EXPECT_EQ(c.On_save_requested(true, true), OverlayAction::SaveAsAndCopyFile);
}

TEST(overlay_controller, H_Copy_WithSelection) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    EXPECT_EQ(c.On_copy_to_clipboard_requested(), OverlayAction::CopyToClipboard);
}

TEST(overlay_controller, H_Save_EmptySelection_ReturnsNone) {
    auto c = Make_controller();
    EXPECT_EQ(c.On_save_requested(false, false), OverlayAction::None);
    EXPECT_EQ(c.On_save_requested(true, false), OverlayAction::None);
}

TEST(overlay_controller, H_Copy_EmptySelection_ReturnsNone) {
    auto c = Make_controller();
    EXPECT_EQ(c.On_copy_to_clipboard_requested(), OverlayAction::None);
}

// ===========================================================================
// Group I — Snap Edges
// ===========================================================================

TEST(overlay_controller, I_AfterPress_SnapEdgesPopulated) {
    auto c = Make_controller();
    std::vector<RectPx> win_rects = {RectPx::From_ltrb(200, 200, 400, 400)};
    for (auto const &m : c.State().cached_monitors) {
        win_rects.push_back(m.bounds);
    }
    std::ignore = c.On_primary_press(No_mods(), {50, 50}, {50, 50}, std::nullopt,
                                     std::nullopt, std::nullopt, {}, win_rects, 0, 0);
    EXPECT_FALSE(c.State().vertical_edges.empty());
    EXPECT_FALSE(c.State().horizontal_edges.empty());
}

TEST(overlay_controller, I_SnapEdgesIncludeWindowEdges) {
    auto c = Make_controller();
    std::vector<RectPx> win_rects = {RectPx::From_ltrb(200, 100, 400, 300)};
    for (auto const &m : c.State().cached_monitors) {
        win_rects.push_back(m.bounds);
    }
    std::ignore =
        c.On_primary_press(No_mods(), {50, 50}, {50, 50}, std::nullopt, std::nullopt,
                           std::nullopt, {}, win_rects, 0, 0); // origin 0,0
    auto const &ve = c.State().vertical_edges;
    // Left (200) and right (400) of window should be in vertical edges
    bool has_200 = std::find(ve.begin(), ve.end(), 200) != ve.end();
    bool has_400 = std::find(ve.begin(), ve.end(), 400) != ve.end();
    EXPECT_TRUE(has_200);
    EXPECT_TRUE(has_400);
}

TEST(overlay_controller, I_DragSnapsToEdge_WithinThreshold) {
    auto c = Make_controller();
    std::vector<RectPx> win_rects = {RectPx::From_ltrb(200, 0, 400, 1080)};
    for (auto const &m : c.State().cached_monitors) {
        win_rects.push_back(m.bounds);
    }
    // Start drag far from edge
    std::ignore = c.On_primary_press(No_mods(), {50, 50}, {50, 50}, std::nullopt,
                                     std::nullopt, std::nullopt, {}, win_rects, 0, 0);
    // Move right side near edge 200 (at 207 — within threshold 10)
    Move(c, {207, 300});
    Release(c, {207, 300});
    EXPECT_EQ(c.State().final_selection.right, 200); // snapped
}

// ===========================================================================
// Group J — Throttle
// ===========================================================================

TEST(overlay_controller, J_TwoMovesWithin16ms_SecondReturnsNone) {
    auto c = Make_controller();
    Press(c, {100, 100});
    // First move at t=100
    OverlayAction first = c.On_pointer_move(No_mods(), {200, 200}, {}, std::nullopt, {},
                                            std::nullopt, 0, 0, 100u);
    EXPECT_EQ(first, OverlayAction::Repaint);
    // Second move same tick
    OverlayAction second = c.On_pointer_move(No_mods(), {210, 210}, {}, std::nullopt,
                                             {}, std::nullopt, 0, 0, 100u);
    EXPECT_EQ(second, OverlayAction::None);
}

TEST(overlay_controller, J_MoveAfter16ms_ReturnsRepaint) {
    auto c = Make_controller();
    Press(c, {100, 100});
    std::ignore = c.On_pointer_move(No_mods(), {200, 200}, {}, std::nullopt, {},
                                    std::nullopt, 0, 0, 100u);
    OverlayAction action = c.On_pointer_move(No_mods(), {210, 210}, {}, std::nullopt,
                                             {}, std::nullopt, 0, 0, 116u);
    EXPECT_EQ(action, OverlayAction::Repaint);
}

// ===========================================================================
// Group K — Alt key
// ===========================================================================

TEST(overlay_controller, K_AltPress_AlwaysRepaint_NoPreviewChange) {
    auto c = Make_controller();
    OverlayAction action = c.On_modifier_changed(Alt_only(), {}, {}, {}, {}, 0, 0);
    EXPECT_EQ(action, OverlayAction::Repaint);
    EXPECT_FALSE(c.State().modifier_preview); // no preview state change
}

TEST(overlay_controller, K_AltRelease_AlwaysRepaint) {
    auto c = Make_controller();
    // Simulate alt was held, now released
    OverlayAction action = c.On_modifier_changed(No_mods(), {}, {}, {}, {}, 0, 0);
    EXPECT_EQ(action, OverlayAction::Repaint);
}

TEST(overlay_controller, AnnotationToolHotkey_TogglesFreehand) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});

    EXPECT_EQ(c.Active_annotation_tool(), std::nullopt);
    EXPECT_EQ(c.On_annotation_tool_hotkey(L'B'), OverlayAction::Repaint);
    EXPECT_EQ(c.Active_annotation_tool(),
              std::optional<AnnotationToolId>{AnnotationToolId::Freehand});
    EXPECT_EQ(c.On_annotation_tool_hotkey(L'B'), OverlayAction::Repaint);
    EXPECT_EQ(c.Active_annotation_tool(), std::nullopt);
}

TEST(overlay_controller, BrushWidthAdjust_IgnoresInactiveBrushTool) {
    auto c = Make_controller();

    EXPECT_EQ(c.Adjust_brush_width(1), std::nullopt);
    Press(c, {100, 100});
    Release(c, {300, 300});
    EXPECT_EQ(c.Adjust_brush_width(1), std::nullopt);
}

TEST(overlay_controller, BrushWidthAdjust_ClampsAndReturnsUpdatedWidth) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    ASSERT_EQ(c.On_annotation_tool_hotkey(L'B'), OverlayAction::Repaint);

    EXPECT_EQ(c.Brush_width_px(), StrokeStyle::kDefaultWidthPx);
    EXPECT_EQ(c.Adjust_brush_width(1), std::optional<int32_t>{3});
    EXPECT_EQ(c.Adjust_brush_width(100), std::optional<int32_t>{50});
    EXPECT_EQ(c.Adjust_brush_width(1), std::nullopt);
    EXPECT_EQ(c.Adjust_brush_width(-100), std::optional<int32_t>{1});
}

TEST(overlay_controller,
     AnnotationToolbar_FreehandStrokeStaysVisibleButNonInteractive) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    ASSERT_EQ(c.On_annotation_tool_hotkey(L'B'), OverlayAction::Repaint);

    EXPECT_TRUE(c.Should_show_annotation_toolbar());
    EXPECT_TRUE(c.Can_interact_with_annotation_toolbar());

    ASSERT_EQ(c.On_primary_press(No_mods(), {120, 120}, {120, 120}, std::nullopt,
                                 std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0,
                                 0),
              OverlayAction::Repaint);
    EXPECT_TRUE(c.Should_show_annotation_toolbar());
    EXPECT_FALSE(c.Can_interact_with_annotation_toolbar());

    EXPECT_EQ(c.On_primary_release(No_mods(), {140, 140}), OverlayAction::Repaint);
    EXPECT_TRUE(c.Should_show_annotation_toolbar());
    EXPECT_TRUE(c.Can_interact_with_annotation_toolbar());
}

TEST(overlay_controller, AnnotationToolbar_MoveAndResizeHideToolbar) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});

    ASSERT_TRUE(c.Should_show_annotation_toolbar());
    ASSERT_TRUE(c.Can_interact_with_annotation_toolbar());

    ASSERT_EQ(c.On_primary_press(No_mods(), {200, 200}, {200, 200}, std::nullopt,
                                 std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0,
                                 0),
              OverlayAction::Repaint);
    EXPECT_FALSE(c.Should_show_annotation_toolbar());
    EXPECT_FALSE(c.Can_interact_with_annotation_toolbar());
    EXPECT_EQ(c.On_primary_release(No_mods(), {220, 220}), OverlayAction::Repaint);
    EXPECT_TRUE(c.Should_show_annotation_toolbar());

    ASSERT_EQ(c.On_primary_press(No_mods(), {100, 100}, {100, 100}, std::nullopt,
                                 std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0,
                                 0),
              OverlayAction::Repaint);
    EXPECT_FALSE(c.Should_show_annotation_toolbar());
    EXPECT_FALSE(c.Can_interact_with_annotation_toolbar());
    EXPECT_EQ(c.On_primary_release(No_mods(), {90, 90}), OverlayAction::Repaint);
    EXPECT_TRUE(c.Should_show_annotation_toolbar());
    EXPECT_TRUE(c.Can_interact_with_annotation_toolbar());
}

TEST(overlay_controller, AnnotationToolbar_AnnotationDragHidesToolbar) {
    auto c = Make_controller();
    Press(c, {100, 100});
    Release(c, {300, 300});
    ASSERT_EQ(c.On_annotation_tool_hotkey(L'B'), OverlayAction::Repaint);
    ASSERT_EQ(c.On_primary_press(No_mods(), {120, 120}, {120, 120}, std::nullopt,
                                 std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0,
                                 0),
              OverlayAction::Repaint);
    ASSERT_EQ(c.On_primary_release(No_mods(), {140, 140}), OverlayAction::Repaint);
    ASSERT_EQ(c.Annotations().size(), 1u);
    ASSERT_EQ(c.On_annotation_tool_hotkey(L'B'), OverlayAction::Repaint);

    ASSERT_TRUE(c.Should_show_annotation_toolbar());
    ASSERT_EQ(c.On_primary_press(No_mods(), {130, 130}, {130, 130}, std::nullopt,
                                 std::nullopt, std::nullopt, {}, Make_vis_rects(c), 0,
                                 0),
              OverlayAction::Repaint);
    EXPECT_FALSE(c.Should_show_annotation_toolbar());
    EXPECT_FALSE(c.Can_interact_with_annotation_toolbar());
    EXPECT_EQ(c.On_primary_release(No_mods(), {150, 150}), OverlayAction::Repaint);
    EXPECT_TRUE(c.Should_show_annotation_toolbar());
    EXPECT_TRUE(c.Can_interact_with_annotation_toolbar());
}
