#include "greenflame_core/overlay_controller.h"

#include "greenflame_core/snap_edge_builder.h"
#include "greenflame_core/snap_to_edges.h"

namespace greenflame::core {

namespace {
constexpr int32_t kSnapThresholdPx = 10;

[[nodiscard]] RectPx Virtual_desktop_bounds_from_monitors(
    std::span<const MonitorWithBounds> monitors) noexcept {
    RectPx bounds = {};
    bool have_bounds = false;
    for (MonitorWithBounds const &monitor : monitors) {
        RectPx const normalized = monitor.bounds.Normalized();
        if (normalized.Is_empty()) {
            continue;
        }
        bounds = have_bounds ? RectPx::Union(bounds, normalized) : normalized;
        have_bounds = true;
    }
    return bounds;
}

[[nodiscard]] RectPx
Resolve_virtual_desktop_client_bounds(RectPx virtual_desktop_bounds,
                                      std::span<const MonitorWithBounds> monitors,
                                      int32_t origin_x, int32_t origin_y) noexcept {
    RectPx const screen_bounds = virtual_desktop_bounds.Is_empty()
                                     ? Virtual_desktop_bounds_from_monitors(monitors)
                                     : virtual_desktop_bounds.Normalized();
    if (screen_bounds.Is_empty()) {
        return {};
    }
    return Screen_rect_to_client_rect(screen_bounds, origin_x, origin_y);
}

[[nodiscard]] RectPx Clip_selection_rect_to_bounds(RectPx rect,
                                                   RectPx bounds) noexcept {
    if (rect.Is_empty() || bounds.Is_empty()) {
        return rect;
    }

    std::optional<RectPx> const clipped = RectPx::Clip(rect.Normalized(), bounds);
    return clipped.value_or(RectPx::From_ltrb(0, 0, 0, 0));
}

[[nodiscard]] RectPx Clamp_moved_selection_to_bounds(RectPx rect,
                                                     RectPx bounds) noexcept {
    if (rect.Is_empty() || bounds.Is_empty()) {
        return rect;
    }

    int32_t const width = rect.Width();
    int32_t const height = rect.Height();
    if (width <= 0 || height <= 0) {
        return rect;
    }

    int32_t const max_left =
        width >= bounds.Width() ? bounds.left : bounds.right - width;
    int32_t const max_top =
        height >= bounds.Height() ? bounds.top : bounds.bottom - height;
    int32_t const left = std::clamp(rect.left, bounds.left, max_left);
    int32_t const top = std::clamp(rect.top, bounds.top, max_top);
    return RectPx::From_ltrb(left, top, left + width, top + height);
}

[[nodiscard]] bool Text_annotation_has_text(TextAnnotation const &annotation) noexcept {
    for (TextRun const &run : annotation.runs) {
        if (!run.text.empty()) {
            return true;
        }
    }
    return false;
}
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
    virtual_desktop_client_bounds = {};
    live_rect = {};
    final_selection = {};
    selection_source = SaveSelectionSource::Region;
    selection_window = std::nullopt;
    selection_monitor_index = std::nullopt;
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
    state_.vertical_edges.reserve(128);
    state_.horizontal_edges.reserve(128);
}

void OverlayController::Refresh_snap_edges(SnapEdges const &visible_snap_edges,
                                           int32_t origin_x, int32_t origin_y) {
    Rebuild_snap_edges(visible_snap_edges, origin_x, origin_y);
}

void OverlayController::Update_virtual_desktop_client_bounds(
    RectPx virtual_desktop_bounds, int32_t origin_x, int32_t origin_y) {
    bool const has_explicit_bounds = !virtual_desktop_bounds.Is_empty();
    bool const has_explicit_origin = origin_x != 0 || origin_y != 0;
    if (!has_explicit_bounds && !has_explicit_origin &&
        !state_.virtual_desktop_client_bounds.Is_empty()) {
        return;
    }

    state_.virtual_desktop_client_bounds = Resolve_virtual_desktop_client_bounds(
        virtual_desktop_bounds, state_.cached_monitors, origin_x, origin_y);
}

void OverlayController::Set_final_selection(RectPx r) {
    state_.final_selection = r;
    if (r.Is_empty()) {
        annotation_controller_.Reset_for_selection_mode();
    }
}

void OverlayController::Push_command(std::unique_ptr<ICommand> cmd) {
    undo_stack_.Push(std::move(cmd));
}

void OverlayController::Undo() { undo_stack_.Undo(); }

void OverlayController::Redo() { undo_stack_.Redo(); }

OverlayAction OverlayController::On_annotation_tool_hotkey(wchar_t hotkey, bool shift) {
    if (state_.final_selection.Is_empty()) {
        return OverlayAction::None;
    }
    return annotation_controller_.Toggle_tool_by_hotkey(hotkey, shift)
               ? OverlayAction::Repaint
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
        return OverlayAction::InvalidateFrozenCache;
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

Annotation const *OverlayController::Selected_annotation() const noexcept {
    return annotation_controller_.Selected_annotation();
}

std::optional<RectPx> OverlayController::Selected_annotation_bounds() const noexcept {
    return annotation_controller_.Selected_annotation_bounds();
}

std::optional<AnnotationEditTarget>
OverlayController::Annotation_edit_target_at(PointPx cursor) const noexcept {
    return annotation_controller_.Annotation_edit_target_at(cursor);
}

std::optional<AnnotationToolId>
OverlayController::Active_annotation_tool() const noexcept {
    return annotation_controller_.Active_tool();
}

void OverlayController::Set_text_layout_engine(ITextLayoutEngine *engine) noexcept {
    annotation_controller_.Set_text_layout_engine(engine);
}

void OverlayController::Set_obfuscate_source_provider(
    IObfuscateSourceProvider *provider) noexcept {
    annotation_controller_.Set_obfuscate_source_provider(provider);
}

bool OverlayController::Has_active_text_edit() const noexcept {
    return annotation_controller_.Has_active_text_edit();
}

TextEditController *OverlayController::Active_text_edit() noexcept {
    return annotation_controller_.Active_text_edit();
}

int32_t OverlayController::Text_point_size() const noexcept {
    return annotation_controller_.Text_point_size();
}

TextFontChoice OverlayController::Text_current_font() const noexcept {
    return annotation_controller_.Text_current_font();
}

void OverlayController::Set_text_current_font(TextFontChoice choice) noexcept {
    annotation_controller_.Set_text_current_font(choice);
}

TextFontChoice OverlayController::Bubble_current_font() const noexcept {
    return annotation_controller_.Bubble_current_font();
}

void OverlayController::Set_bubble_current_font(TextFontChoice choice) noexcept {
    annotation_controller_.Set_bubble_current_font(choice);
}

bool OverlayController::Commit_active_text_edit() {
    TextEditController *const edit = annotation_controller_.Active_text_edit();
    if (edit == nullptr) {
        return false;
    }

    annotation_controller_.Commit_text_annotation(undo_stack_, edit->Commit());
    return true;
}

void OverlayController::Cancel_text_draft() {
    annotation_controller_.Cancel_text_draft();
}

bool OverlayController::Has_active_annotation_edit() const noexcept {
    return annotation_controller_.Has_active_edit_interaction();
}

std::optional<AnnotationEditHandleKind>
OverlayController::Active_annotation_edit_handle() const noexcept {
    return annotation_controller_.Active_annotation_edit_handle();
}

std::vector<size_t> OverlayController::Active_obfuscate_preview_indices() const {
    return annotation_controller_.Active_obfuscate_preview_indices();
}

COLORREF OverlayController::Annotation_color() const noexcept {
    return annotation_controller_.Annotation_color();
}

COLORREF OverlayController::Brush_annotation_color() const noexcept {
    return annotation_controller_.Brush_annotation_color();
}

COLORREF OverlayController::Highlighter_color() const noexcept {
    return annotation_controller_.Highlighter_color();
}

int32_t OverlayController::Highlighter_opacity_percent() const noexcept {
    return annotation_controller_.Highlighter_opacity_percent();
}

void OverlayController::Set_tool_size_step(AnnotationToolId tool,
                                           int32_t step) noexcept {
    (void)annotation_controller_.Set_tool_size_step(tool, step);
}

int32_t OverlayController::Tool_size_step(AnnotationToolId tool) const noexcept {
    return annotation_controller_.Tool_size_step(tool);
}

int32_t OverlayController::Tool_physical_size(AnnotationToolId tool) const noexcept {
    return annotation_controller_.Tool_physical_size(tool);
}

void OverlayController::Set_annotation_color(COLORREF color) noexcept {
    (void)annotation_controller_.Set_annotation_color(color);
}

void OverlayController::Set_brush_annotation_color(COLORREF color) noexcept {
    (void)annotation_controller_.Set_brush_annotation_color(color);
}

void OverlayController::Set_highlighter_color(COLORREF color) noexcept {
    (void)annotation_controller_.Set_highlighter_color(color);
}

void OverlayController::Set_highlighter_opacity_percent(
    int32_t opacity_percent) noexcept {
    (void)annotation_controller_.Set_highlighter_opacity_percent(opacity_percent);
}

std::optional<int32_t> OverlayController::Adjust_tool_size(int32_t delta_steps) {
    std::optional<AnnotationToolId> const active_tool =
        annotation_controller_.Active_tool();
    if (delta_steps == 0 || state_.final_selection.Is_empty() ||
        !active_tool.has_value()) {
        return std::nullopt;
    }
    switch (*active_tool) {
    case AnnotationToolId::Freehand:
    case AnnotationToolId::Highlighter:
    case AnnotationToolId::Line:
    case AnnotationToolId::Arrow:
    case AnnotationToolId::Rectangle:
    case AnnotationToolId::Ellipse:
    case AnnotationToolId::Bubble:
    case AnnotationToolId::Obfuscate:
        break;
    case AnnotationToolId::Text:
        if (annotation_controller_.Has_active_text_edit()) {
            return std::nullopt;
        }
        break;
    case AnnotationToolId::FilledRectangle:
    case AnnotationToolId::FilledEllipse:
        return std::nullopt;
    }
    int32_t const current_step = annotation_controller_.Tool_size_step(*active_tool);
    if (!annotation_controller_.Set_tool_size_step(*active_tool,
                                                   current_step + delta_steps)) {
        return std::nullopt;
    }
    return annotation_controller_.Tool_physical_size(*active_tool);
}

bool OverlayController::Should_show_annotation_toolbar() const noexcept {
    return !state_.final_selection.Is_empty() && !state_.dragging &&
           !state_.handle_dragging && !state_.move_dragging;
}

bool OverlayController::Can_interact_with_annotation_toolbar() const noexcept {
    return Should_show_annotation_toolbar() &&
           !annotation_controller_.Has_active_text_edit() &&
           !annotation_controller_.Has_active_gesture();
}

bool OverlayController::Should_show_selected_annotation_handles() const noexcept {
    return annotation_controller_.Selected_annotation() != nullptr;
}

bool OverlayController::Has_active_annotation_gesture() const noexcept {
    return annotation_controller_.Has_active_gesture();
}

bool OverlayController::Straighten_highlighter_stroke() noexcept {
    return annotation_controller_.Straighten_highlighter_stroke();
}

bool OverlayController::Is_annotation_dragging() const noexcept {
    return annotation_controller_.Is_annotation_dragging();
}

bool OverlayController::Has_annotation_at(PointPx cursor) const noexcept {
    return annotation_controller_.Annotation_id_at(cursor).has_value();
}

void OverlayController::Rebuild_snap_edges(SnapEdges const &screen_edges,
                                           int32_t origin_x, int32_t origin_y) {
    SnapEdges const edges =
        Screen_snap_edges_to_client_snap_edges(screen_edges, origin_x, origin_y);
    state_.vertical_edges = edges.vertical;
    state_.horizontal_edges = edges.horizontal;
}

void OverlayController::Apply_modifier_preview(OverlayModifierState mods,
                                               PointPx /*cursor_screen*/,
                                               std::optional<RectPx> window_rect_screen,
                                               RectPx /*virtual_desktop_bounds*/,
                                               std::optional<size_t> monitor_index,
                                               int32_t origin_x, int32_t origin_y) {
    if (state_.dragging || state_.handle_dragging) {
        return;
    }
    if (!state_.final_selection.Is_empty()) {
        return;
    }

    if (mods.shift && mods.ctrl) {
        state_.live_rect = state_.virtual_desktop_client_bounds;
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
    Update_virtual_desktop_client_bounds(virtual_desktop_bounds, origin_x, origin_y);
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
    if (annotation_controller_.Has_active_text_edit()) {
        annotation_controller_.Cancel_text_draft();
        return OverlayAction::Repaint;
    }
    if (std::optional<AnnotationToolId> const active_tool =
            annotation_controller_.Active_tool();
        active_tool.has_value()) {
        if (annotation_controller_.Has_active_tool_gesture()) {
            if (annotation_controller_.On_cancel()) {
                return OverlayAction::Repaint;
            }
        } else if (annotation_controller_.Toggle_tool(*active_tool)) {
            return OverlayAction::Repaint;
        }
    }
    if (annotation_controller_.Set_selected_annotation(std::nullopt)) {
        return OverlayAction::Repaint;
    }
    if (!state_.final_selection.Is_empty()) {
        Set_final_selection({});
        state_.live_rect = {};
        return OverlayAction::InvalidateFrozenCache;
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
    std::optional<RectPx> /*window_rect_screen*/, RectPx virtual_desktop_bounds,
    SnapEdges const &visible_snap_edges, int32_t origin_x, int32_t origin_y) {
    Update_virtual_desktop_client_bounds(virtual_desktop_bounds, origin_x, origin_y);

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
        return OverlayAction::InvalidateFrozenCache;
    }

    // ---- When a committed selection exists, resolve resize / tool / move ----
    if (!state_.final_selection.Is_empty() && !state_.dragging &&
        !state_.handle_dragging && !state_.move_dragging) {
        std::optional<AnnotationToolId> const active_tool =
            annotation_controller_.Active_tool();
        if (active_tool.has_value() && *active_tool == AnnotationToolId::Text) {
            if (TextEditController *const edit =
                    annotation_controller_.Active_text_edit();
                edit != nullptr) {
                TextDraftView const view = edit->Build_view();
                if (!view.Hit_bounds().Contains(cursor_client)) {
                    bool const has_text = view.annotation != nullptr &&
                                          Text_annotation_has_text(*view.annotation);
                    if (has_text) {
                        annotation_controller_.Commit_text_annotation(undo_stack_,
                                                                      edit->Commit());
                    } else {
                        annotation_controller_.Cancel_text_draft();
                    }
                    annotation_controller_.Begin_text_draft(cursor_client);
                    return has_text ? OverlayAction::InvalidateFrozenCache
                                    : OverlayAction::Repaint;
                }
            }
        }

        std::optional<SelectionHandle> const hit =
            Hit_test_border_zone(state_.final_selection, cursor_client);
        if (hit.has_value()) {
            state_.handle_dragging = true;
            state_.resize_handle = hit;
            state_.resize_anchor_rect = state_.final_selection;
            state_.live_rect = state_.final_selection;
            Rebuild_snap_edges(visible_snap_edges, origin_x, origin_y);
            return OverlayAction::Repaint;
        }

        if (active_tool.has_value()) {
            if (*active_tool == AnnotationToolId::Text &&
                annotation_controller_.Active_text_edit() == nullptr) {
                annotation_controller_.Begin_text_draft(cursor_client);
                return OverlayAction::Repaint;
            }
            return annotation_controller_.On_primary_press(cursor_client)
                       ? OverlayAction::Repaint
                       : OverlayAction::None;
        }

        if (std::optional<AnnotationEditTarget> const target =
                annotation_controller_.Annotation_edit_target_at(cursor_client);
            target.has_value()) {
            return annotation_controller_.Begin_annotation_edit(*target, cursor_client)
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
            Rebuild_snap_edges(visible_snap_edges, origin_x, origin_y);
            return OverlayAction::Repaint;
        }

        return selection_changed ? OverlayAction::Repaint : OverlayAction::None;
    }

    // ---- Fresh drag ----
    Rebuild_snap_edges(visible_snap_edges, origin_x, origin_y);
    bool const snap_enabled = !mods.alt;
    PointPx snapped_start = cursor_client;
    if (snap_enabled) {
        snapped_start = Snap_point_to_fullscreen_crosshair_edges(
            cursor_client, state_.vertical_edges, state_.horizontal_edges,
            kSnapThresholdPx);
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
    int32_t origin_y) {
    Update_virtual_desktop_client_bounds(virtual_desktop_bounds, origin_x, origin_y);

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
        candidate = Clamp_moved_selection_to_bounds(
            candidate, state_.virtual_desktop_client_bounds);
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
        candidate = Clip_selection_rect_to_bounds(candidate,
                                                  state_.virtual_desktop_client_bounds);
        state_.live_rect =
            Allowed_selection_rect(candidate, anchor, state_.cached_monitors);
    } else if (state_.dragging) {
        RectPx candidate =
            RectPx::From_points(state_.start_px, cursor_client).Normalized();
        if (snap_enabled) {
            candidate = Snap_rect_to_edges(candidate, state_.vertical_edges,
                                           state_.horizontal_edges, kSnapThresholdPx);
        }
        candidate = Clip_selection_rect_to_bounds(candidate,
                                                  state_.virtual_desktop_client_bounds);
        state_.live_rect = candidate;
    } else if (annotation_controller_.Has_active_text_edit()) {
        if (TextEditController *const text_edit =
                annotation_controller_.Active_text_edit();
            text_edit != nullptr) {
            text_edit->On_pointer_move(cursor_client, mods.primary_down);
        }
    } else if (annotation_controller_.Has_active_gesture()) {
        // Annotation gestures update their own draft/live state here; repaint cadence
        // is controlled by the shared throttle below.
        (void)annotation_controller_.On_pointer_move(cursor_client, mods.primary_down);
    } else {
        Apply_modifier_preview(mods, cursor_screen, window_rect_screen,
                               virtual_desktop_bounds, monitor_index_under_cursor,
                               origin_x, origin_y);
    }

    return OverlayAction::Repaint;
}

OverlayAction OverlayController::On_primary_release(OverlayModifierState mods,
                                                    PointPx cursor_client) {
    bool const snap_enabled = !mods.alt;

    if (state_.move_dragging) {
        RectPx to_commit = Clamp_moved_selection_to_bounds(
            state_.live_rect, state_.virtual_desktop_client_bounds);
        if (snap_enabled) {
            to_commit =
                Snap_moved_rect_to_edges(to_commit, state_.vertical_edges,
                                         state_.horizontal_edges, kSnapThresholdPx);
        }
        to_commit = Clamp_moved_selection_to_bounds(
            to_commit, state_.virtual_desktop_client_bounds);
        PointPx const center = {to_commit.left + to_commit.Width() / 2,
                                to_commit.top + to_commit.Height() / 2};
        state_.final_selection =
            Allowed_selection_rect(to_commit, center, state_.cached_monitors);
        state_.move_dragging = false;
        state_.live_rect = {};
        return OverlayAction::InvalidateFrozenCache;
    }

    if (state_.handle_dragging && state_.resize_handle.has_value()) {
        RectPx to_commit = Clip_selection_rect_to_bounds(
            state_.live_rect, state_.virtual_desktop_client_bounds);
        if (snap_enabled) {
            to_commit = Snap_rect_to_edges(to_commit, state_.vertical_edges,
                                           state_.horizontal_edges, kSnapThresholdPx);
        }
        to_commit = Clip_selection_rect_to_bounds(to_commit,
                                                  state_.virtual_desktop_client_bounds);
        PointPx const anchor = Anchor_point_for_resize_policy(state_.resize_anchor_rect,
                                                              *state_.resize_handle);
        state_.final_selection =
            Allowed_selection_rect(to_commit, anchor, state_.cached_monitors);
        state_.handle_dragging = false;
        state_.resize_handle = std::nullopt;
        state_.live_rect = {};
        return OverlayAction::InvalidateFrozenCache;
    }

    if (state_.dragging) {
        RectPx raw = RectPx::From_points(state_.start_px, cursor_client).Normalized();
        if (snap_enabled) {
            raw = Snap_rect_to_edges(raw, state_.vertical_edges,
                                     state_.horizontal_edges, kSnapThresholdPx);
        }
        raw = Clip_selection_rect_to_bounds(raw, state_.virtual_desktop_client_bounds);
        state_.final_selection =
            Allowed_selection_rect(raw, state_.start_px, state_.cached_monitors);
        state_.selection_source = SaveSelectionSource::Region;
        state_.selection_window = std::nullopt;
        state_.selection_monitor_index = std::nullopt;
        state_.dragging = false;
        return OverlayAction::InvalidateFrozenCache;
    }

    if (annotation_controller_.Has_active_text_edit()) {
        if (TextEditController *const text_edit =
                annotation_controller_.Active_text_edit();
            text_edit != nullptr) {
            text_edit->On_pointer_release(cursor_client);
            return OverlayAction::Repaint;
        }
    }

    if (annotation_controller_.Has_active_gesture()) {
        (void)annotation_controller_.On_pointer_move(cursor_client, mods.primary_down);
        return annotation_controller_.On_primary_release(undo_stack_)
                   ? OverlayAction::InvalidateFrozenCache
                   : OverlayAction::None;
    }

    return OverlayAction::None;
}

} // namespace greenflame::core
