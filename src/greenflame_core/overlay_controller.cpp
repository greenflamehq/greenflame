#include "greenflame_core/overlay_controller.h"

#include "greenflame_core/snap_edge_builder.h"
#include "greenflame_core/snap_to_edges.h"

namespace greenflame::core {

namespace {
constexpr int32_t kSnapThresholdPx = 10;
} // namespace

// ---------------------------------------------------------------------------
// OverlaySessionData
// ---------------------------------------------------------------------------

void OverlaySessionData::Reset_for_session() {
    dragging = false;
    handle_dragging = false;
    move_dragging = false;
    modifier_preview = false;
    resize_handle = std::nullopt;
    resize_anchor_rect = {};
    move_grab_offset = {};
    move_anchor_rect = {};
    start_px = {};
    live_rect = {};
    final_selection = {};
    selection_source = SaveSelectionSource::Region;
    selection_window = std::nullopt;
    selection_monitor_index = std::nullopt;
    last_invalidate_tick = 0;
    window_rects.clear();
    vertical_edges.clear();
    horizontal_edges.clear();
    cached_monitors.clear();
}

// ---------------------------------------------------------------------------
// OverlayController — private helpers
// ---------------------------------------------------------------------------

void OverlayController::Reset_for_session(std::vector<MonitorWithBounds> monitors) {
    state_.Reset_for_session();
    undo_stack_.Clear();
    annotation_controller_.Reset_for_session();
    state_.cached_monitors = std::move(monitors);
    state_.window_rects.reserve(64);
    state_.vertical_edges.reserve(128);
    state_.horizontal_edges.reserve(128);
}

void OverlayController::Set_final_selection(RectPx r) {
    state_.final_selection = r;
    if (r.Is_empty()) {
        annotation_controller_.Clear_annotations();
    }
}

void OverlayController::Push_command(std::unique_ptr<ICommand> cmd) {
    undo_stack_.Push(std::move(cmd));
}

void OverlayController::Undo() { undo_stack_.Undo(); }

void OverlayController::Redo() { undo_stack_.Redo(); }

OverlayAction OverlayController::On_annotation_tool_hotkey(wchar_t hotkey) {
    if (state_.final_selection.Is_empty()) {
        return OverlayAction::None;
    }
    return annotation_controller_.Toggle_tool_by_hotkey(hotkey) ? OverlayAction::Repaint
                                                                : OverlayAction::None;
}

OverlayAction OverlayController::On_select_annotation_tool(AnnotationToolId id) {
    if (state_.final_selection.Is_empty()) {
        return OverlayAction::None;
    }
    return annotation_controller_.Toggle_tool(id) ? OverlayAction::Repaint
                                                  : OverlayAction::None;
}

OverlayAction OverlayController::On_delete_selected_annotation() {
    if (annotation_controller_.Delete_selected_annotation(undo_stack_)) {
        return OverlayAction::Repaint;
    }
    return OverlayAction::None;
}

std::vector<AnnotationToolbarButtonView>
OverlayController::Build_annotation_toolbar_button_views() const {
    if (state_.final_selection.Is_empty()) {
        return {};
    }
    return annotation_controller_.Build_toolbar_button_views();
}

std::span<const Annotation> OverlayController::Annotations() const noexcept {
    return annotation_controller_.Annotations();
}

Annotation const *OverlayController::Draft_annotation() const noexcept {
    return annotation_controller_.Draft_annotation();
}

std::span<const PointPx> OverlayController::Draft_freehand_points() const noexcept {
    return annotation_controller_.Draft_freehand_points();
}

std::optional<StrokeStyle> OverlayController::Draft_freehand_style() const noexcept {
    return annotation_controller_.Draft_freehand_style();
}

std::optional<RectPx> OverlayController::Selected_annotation_bounds() const noexcept {
    return annotation_controller_.Selected_annotation_bounds();
}

std::optional<AnnotationToolId>
OverlayController::Active_annotation_tool() const noexcept {
    return annotation_controller_.Active_tool();
}

int32_t OverlayController::Brush_width_px() const noexcept {
    return annotation_controller_.Brush_width_px();
}

void OverlayController::Set_brush_width_px(int32_t width_px) noexcept {
    (void)annotation_controller_.Set_brush_width_px(width_px);
}

std::optional<int32_t> OverlayController::Adjust_brush_width(int32_t delta_steps) {
    if (delta_steps == 0 || state_.final_selection.Is_empty() ||
        annotation_controller_.Active_tool() !=
            std::optional<AnnotationToolId>{AnnotationToolId::Freehand}) {
        return std::nullopt;
    }
    int32_t const updated_width =
        annotation_controller_.Brush_width_px() + delta_steps;
    if (!annotation_controller_.Set_brush_width_px(updated_width)) {
        return std::nullopt;
    }
    return annotation_controller_.Brush_width_px();
}

bool OverlayController::Should_show_annotation_toolbar() const noexcept {
    return !state_.final_selection.Is_empty() && !state_.dragging &&
           !state_.handle_dragging && !state_.move_dragging &&
           !annotation_controller_.Is_annotation_dragging();
}

bool OverlayController::Can_interact_with_annotation_toolbar() const noexcept {
    return Should_show_annotation_toolbar() &&
           !annotation_controller_.Has_active_gesture();
}

bool OverlayController::Has_active_annotation_gesture() const noexcept {
    return annotation_controller_.Has_active_gesture();
}

bool OverlayController::Is_annotation_dragging() const noexcept {
    return annotation_controller_.Is_annotation_dragging();
}

bool OverlayController::Has_annotation_at(PointPx cursor) const noexcept {
    return annotation_controller_.Annotation_id_at(cursor).has_value();
}

void OverlayController::Rebuild_snap_edges(std::vector<RectPx> window_rects,
                                           int32_t origin_x, int32_t origin_y) {
    SnapEdges const edges =
        Build_snap_edges_from_screen_rects(window_rects, origin_x, origin_y);
    state_.window_rects = std::move(window_rects);
    state_.vertical_edges = edges.vertical;
    state_.horizontal_edges = edges.horizontal;
}

void OverlayController::Apply_modifier_preview(OverlayModifierState mods,
                                               PointPx /*cursor_screen*/,
                                               std::optional<RectPx> window_rect_screen,
                                               RectPx virtual_desktop_bounds,
                                               std::optional<size_t> monitor_index,
                                               int32_t origin_x, int32_t origin_y) {
    if (state_.dragging || state_.handle_dragging) {
        return;
    }
    if (!state_.final_selection.Is_empty()) {
        return;
    }

    if (mods.shift && mods.ctrl) {
        state_.live_rect =
            Screen_rect_to_client_rect(virtual_desktop_bounds, origin_x, origin_y);
        state_.modifier_preview = true;
    } else if (mods.shift) {
        if (monitor_index.has_value() &&
            *monitor_index < state_.cached_monitors.size()) {
            state_.live_rect = Screen_rect_to_client_rect(
                state_.cached_monitors[*monitor_index].bounds, origin_x, origin_y);
        } else {
            state_.live_rect = {};
        }
        state_.modifier_preview = true;
    } else if (mods.ctrl) {
        if (window_rect_screen.has_value()) {
            state_.live_rect =
                Screen_rect_to_client_rect(*window_rect_screen, origin_x, origin_y);
        } else {
            state_.live_rect = {};
        }
        state_.modifier_preview = true;
    } else {
        if (state_.modifier_preview) {
            state_.modifier_preview = false;
            state_.live_rect = {};
        }
    }
}

// ---------------------------------------------------------------------------
// OverlayController — public API
// ---------------------------------------------------------------------------

OverlayAction OverlayController::On_modifier_changed(
    OverlayModifierState new_mods, PointPx cursor_screen,
    std::optional<RectPx> window_rect_screen, RectPx virtual_desktop_bounds,
    std::optional<size_t> monitor_index_under_cursor, int32_t origin_x,
    int32_t origin_y) {
    Apply_modifier_preview(new_mods, cursor_screen, window_rect_screen,
                           virtual_desktop_bounds, monitor_index_under_cursor, origin_x,
                           origin_y);
    return OverlayAction::Repaint;
}

OverlayAction OverlayController::On_cancel() {
    if (state_.move_dragging) {
        state_.final_selection = state_.move_anchor_rect;
        state_.move_dragging = false;
        state_.live_rect = {};
        return OverlayAction::Repaint;
    }
    if (state_.handle_dragging) {
        state_.handle_dragging = false;
        state_.resize_handle = std::nullopt;
        state_.live_rect = {};
        return OverlayAction::Repaint;
    }
    if (state_.dragging) {
        state_.dragging = false;
        state_.live_rect = {};
        return OverlayAction::Repaint;
    }
    if (annotation_controller_.On_cancel()) {
        return OverlayAction::Repaint;
    }
    if (!state_.final_selection.Is_empty()) {
        Set_final_selection({});
        state_.live_rect = {};
        return OverlayAction::Repaint;
    }
    return OverlayAction::Close;
}

OverlayAction OverlayController::On_save_requested(bool save_as, bool copy_file_also) {
    if (state_.final_selection.Is_empty()) {
        return OverlayAction::None;
    }
    if (save_as) {
        return copy_file_also ? OverlayAction::SaveAsAndCopyFile
                              : OverlayAction::SaveAs;
    }
    return copy_file_also ? OverlayAction::SaveDirectAndCopyFile
                          : OverlayAction::SaveDirect;
}

OverlayAction OverlayController::On_copy_to_clipboard_requested() {
    if (state_.final_selection.Is_empty()) {
        return OverlayAction::None;
    }
    return OverlayAction::CopyToClipboard;
}

OverlayAction OverlayController::On_primary_press(
    OverlayModifierState mods, PointPx cursor_client, PointPx /*cursor_screen*/,
    std::optional<HWND> window_under_cursor,
    std::optional<size_t> monitor_index_under_cursor,
    std::optional<RectPx> /*window_rect_screen*/, RectPx /*virtual_desktop_bounds*/,
    std::vector<RectPx> visible_window_rects, int32_t origin_x, int32_t origin_y) {

    // ---- Modifier-preview commit path ----
    if ((mods.shift || mods.ctrl) && state_.modifier_preview) {
        state_.final_selection = state_.live_rect;
        state_.selection_window = std::nullopt;
        state_.selection_monitor_index = std::nullopt;
        if (mods.shift && mods.ctrl) {
            state_.selection_source = SaveSelectionSource::Desktop;
        } else if (mods.shift) {
            state_.selection_source = SaveSelectionSource::Monitor;
            if (monitor_index_under_cursor.has_value()) {
                state_.selection_monitor_index = *monitor_index_under_cursor;
            }
        } else { // ctrl only
            state_.selection_source = SaveSelectionSource::Window;
            if (window_under_cursor.has_value()) {
                state_.selection_window = *window_under_cursor;
            }
        }
        state_.modifier_preview = false;
        state_.live_rect = {};
        return OverlayAction::Repaint;
    }

    // ---- When a committed selection exists, resolve resize / tool / move ----
    if (!state_.final_selection.Is_empty() && !state_.dragging &&
        !state_.handle_dragging && !state_.move_dragging) {
        std::optional<SelectionHandle> hit =
            Hit_test_border_zone(state_.final_selection, cursor_client);
        if (hit.has_value()) {
            state_.handle_dragging = true;
            state_.resize_handle = hit;
            state_.resize_anchor_rect = state_.final_selection;
            state_.live_rect = state_.final_selection;
            Rebuild_snap_edges(std::move(visible_window_rects), origin_x, origin_y);
            return OverlayAction::Repaint;
        }

        if (annotation_controller_.Has_active_tool()) {
            return annotation_controller_.On_primary_press(cursor_client)
                       ? OverlayAction::Repaint
                       : OverlayAction::None;
        }

        if (std::optional<uint64_t> const annotation_id =
                annotation_controller_.Annotation_id_at(cursor_client);
            annotation_id.has_value()) {
            return annotation_controller_.Begin_annotation_drag(*annotation_id,
                                                                cursor_client)
                       ? OverlayAction::Repaint
                       : OverlayAction::None;
        }

        bool const selection_changed =
            annotation_controller_.Set_selected_annotation(std::nullopt);
        if (state_.final_selection.Contains(cursor_client)) {
            state_.move_dragging = true;
            state_.move_grab_offset = {cursor_client.x - state_.final_selection.left,
                                       cursor_client.y - state_.final_selection.top};
            state_.move_anchor_rect = state_.final_selection;
            state_.live_rect = state_.final_selection;
            Rebuild_snap_edges(std::move(visible_window_rects), origin_x, origin_y);
            return OverlayAction::Repaint;
        }

        return selection_changed ? OverlayAction::Repaint : OverlayAction::None;
    }

    // ---- Fresh drag ----
    Rebuild_snap_edges(std::move(visible_window_rects), origin_x, origin_y);
    bool const snap_enabled = !mods.alt;
    PointPx snapped_start = cursor_client;
    if (snap_enabled) {
        snapped_start = Snap_point_to_edges(cursor_client, state_.vertical_edges,
                                            state_.horizontal_edges, kSnapThresholdPx);
    }
    state_.start_px = snapped_start;
    state_.dragging = true;
    state_.final_selection = {};
    state_.selection_source = SaveSelectionSource::Region;
    state_.selection_window = std::nullopt;
    state_.selection_monitor_index = std::nullopt;
    state_.live_rect = RectPx::From_points(state_.start_px, state_.start_px);
    return OverlayAction::Repaint;
}

OverlayAction OverlayController::On_pointer_move(
    OverlayModifierState mods, PointPx cursor_client, PointPx cursor_screen,
    std::optional<RectPx> window_rect_screen, RectPx virtual_desktop_bounds,
    std::optional<size_t> monitor_index_under_cursor, int32_t origin_x,
    int32_t origin_y, uint64_t now_ms) {

    bool const snap_enabled = !mods.alt;

    if (state_.move_dragging) {
        int32_t const new_left = cursor_client.x - state_.move_grab_offset.x;
        int32_t const new_top = cursor_client.y - state_.move_grab_offset.y;
        RectPx candidate = RectPx::From_ltrb(
            new_left, new_top, new_left + state_.move_anchor_rect.Width(),
            new_top + state_.move_anchor_rect.Height());
        if (snap_enabled) {
            candidate =
                Snap_moved_rect_to_edges(candidate, state_.vertical_edges,
                                         state_.horizontal_edges, kSnapThresholdPx);
        }
        state_.live_rect = candidate;
    } else if (state_.handle_dragging && state_.resize_handle.has_value()) {
        RectPx candidate = Resize_rect_from_handle(
            state_.resize_anchor_rect, *state_.resize_handle, cursor_client);
        if (snap_enabled) {
            candidate = Snap_rect_to_edges(candidate, state_.vertical_edges,
                                           state_.horizontal_edges, kSnapThresholdPx);
        }
        PointPx const anchor = Anchor_point_for_resize_policy(state_.resize_anchor_rect,
                                                              *state_.resize_handle);
        state_.live_rect =
            Allowed_selection_rect(candidate, anchor, state_.cached_monitors);
    } else if (state_.dragging) {
        RectPx candidate =
            RectPx::From_points(state_.start_px, cursor_client).Normalized();
        if (snap_enabled) {
            candidate = Snap_rect_to_edges(candidate, state_.vertical_edges,
                                           state_.horizontal_edges, kSnapThresholdPx);
        }
        state_.live_rect = candidate;
    } else if (annotation_controller_.Has_active_gesture()) {
        // Annotation gestures update their own draft/live state here; repaint cadence
        // is controlled by the shared throttle below.
        (void)annotation_controller_.On_pointer_move(cursor_client);
    } else {
        Apply_modifier_preview(mods, cursor_screen, window_rect_screen,
                               virtual_desktop_bounds, monitor_index_under_cursor,
                               origin_x, origin_y);
    }

    if (now_ms - state_.last_invalidate_tick >= 16) {
        state_.last_invalidate_tick = now_ms;
        return OverlayAction::Repaint;
    }
    return OverlayAction::None;
}

OverlayAction OverlayController::On_primary_release(OverlayModifierState mods,
                                                    PointPx cursor_client) {
    bool const snap_enabled = !mods.alt;

    if (state_.move_dragging) {
        RectPx to_commit = state_.live_rect;
        if (snap_enabled) {
            to_commit =
                Snap_moved_rect_to_edges(to_commit, state_.vertical_edges,
                                         state_.horizontal_edges, kSnapThresholdPx);
        }
        PointPx const center = {to_commit.left + to_commit.Width() / 2,
                                to_commit.top + to_commit.Height() / 2};
        state_.final_selection =
            Allowed_selection_rect(to_commit, center, state_.cached_monitors);
        state_.move_dragging = false;
        state_.live_rect = {};
        return OverlayAction::Repaint;
    }

    if (state_.handle_dragging && state_.resize_handle.has_value()) {
        RectPx to_commit = state_.live_rect;
        if (snap_enabled) {
            to_commit = Snap_rect_to_edges(to_commit, state_.vertical_edges,
                                           state_.horizontal_edges, kSnapThresholdPx);
        }
        PointPx const anchor = Anchor_point_for_resize_policy(state_.resize_anchor_rect,
                                                              *state_.resize_handle);
        state_.final_selection =
            Allowed_selection_rect(to_commit, anchor, state_.cached_monitors);
        state_.handle_dragging = false;
        state_.resize_handle = std::nullopt;
        state_.live_rect = {};
        return OverlayAction::Repaint;
    }

    if (state_.dragging) {
        RectPx raw = RectPx::From_points(state_.start_px, cursor_client).Normalized();
        if (snap_enabled) {
            raw = Snap_rect_to_edges(raw, state_.vertical_edges,
                                     state_.horizontal_edges, kSnapThresholdPx);
        }
        state_.final_selection =
            Allowed_selection_rect(raw, state_.start_px, state_.cached_monitors);
        state_.selection_source = SaveSelectionSource::Region;
        state_.selection_window = std::nullopt;
        state_.selection_monitor_index = std::nullopt;
        state_.dragging = false;
        return OverlayAction::Repaint;
    }

    if (annotation_controller_.Has_active_gesture()) {
        return annotation_controller_.On_primary_release(undo_stack_)
                   ? OverlayAction::Repaint
                   : OverlayAction::None;
    }

    return OverlayAction::None;
}

} // namespace greenflame::core
