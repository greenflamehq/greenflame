#pragma once

#include "greenflame_core/annotation_controller.h"
#include "greenflame_core/command.h"
#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/save_image_policy.h"
#include "greenflame_core/selection_handles.h"
#include "greenflame_core/snap_edge_builder.h"
#include "greenflame_core/undo_stack.h"

namespace greenflame::core {

struct OverlayModifierState {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

enum class OverlayAction : uint8_t {
    None,
    Repaint,
    InvalidateFrozenCache,
    Close,
    SaveDirect,            // Ctrl+S
    SaveDirectAndCopyFile, // Ctrl+Alt+S
    SaveAs,                // Ctrl+Shift+S
    SaveAsAndCopyFile,     // Ctrl+Shift+Alt+S
    CopyToClipboard,       // Ctrl+C
};

struct OverlaySessionData {
    RectPx resize_anchor_rect = {};
    PointPx move_grab_offset = {};
    RectPx move_anchor_rect = {};
    PointPx start_px = {};
    RectPx live_rect = {};
    RectPx final_selection = {};
    std::vector<SnapEdgeSegmentPx> vertical_edges = {};
    std::vector<SnapEdgeSegmentPx> horizontal_edges = {};
    std::vector<MonitorWithBounds> cached_monitors = {};
    std::optional<HWND> selection_window = std::nullopt;
    std::optional<size_t> selection_monitor_index = std::nullopt;
    std::optional<SelectionHandle> resize_handle = std::nullopt;
    bool dragging = false;
    bool handle_dragging = false;
    bool move_dragging = false;
    bool modifier_preview = false;
    SaveSelectionSource selection_source = SaveSelectionSource::Region;

    void Reset_for_session();
};

class OverlayController final {
  public:
    OverlayController() = default;
    OverlayController(OverlayController const &) = delete;
    OverlayController &operator=(OverlayController const &) = delete;
    OverlayController(OverlayController &&) = default;
    OverlayController &operator=(OverlayController &&) = default;

    void Reset_for_session(std::vector<MonitorWithBounds> monitors);
    void Refresh_snap_edges(SnapEdges const &visible_snap_edges, int32_t origin_x,
                            int32_t origin_y);

    // WM_LBUTTONDOWN: all Win32 queries are pre-resolved by caller.
    [[nodiscard]] OverlayAction
    On_primary_press(OverlayModifierState mods, PointPx cursor_client,
                     PointPx cursor_screen, std::optional<HWND> window_under_cursor,
                     std::optional<size_t> monitor_index_under_cursor,
                     std::optional<RectPx> window_rect_screen,
                     RectPx virtual_desktop_bounds, SnapEdges const &visible_snap_edges,
                     int32_t origin_x, int32_t origin_y);

    // WM_MOUSEMOVE
    [[nodiscard]] OverlayAction
    On_pointer_move(OverlayModifierState mods, PointPx cursor_client,
                    PointPx cursor_screen, std::optional<RectPx> window_rect_screen,
                    RectPx virtual_desktop_bounds,
                    std::optional<size_t> monitor_index_under_cursor, int32_t origin_x,
                    int32_t origin_y);

    // WM_LBUTTONUP
    [[nodiscard]] OverlayAction On_primary_release(OverlayModifierState mods,
                                                   PointPx cursor_client);

    // Escape key
    [[nodiscard]] OverlayAction On_cancel();

    // Ctrl+S variants; returns None if selection is empty.
    [[nodiscard]] OverlayAction On_save_requested(bool save_as, bool copy_file_also);

    // Ctrl+C; returns None if selection is empty.
    [[nodiscard]] OverlayAction On_copy_to_clipboard_requested();

    // Shift/Ctrl/Alt key-down or key-up.
    // new_mods = full resolved modifier state after the event.
    // Hints are empty on key-up (preview is cleared, not updated).
    [[nodiscard]] OverlayAction
    On_modifier_changed(OverlayModifierState new_mods, PointPx cursor_screen,
                        std::optional<RectPx> window_rect_screen,
                        RectPx virtual_desktop_bounds,
                        std::optional<size_t> monitor_index_under_cursor,
                        int32_t origin_x, int32_t origin_y);

    [[nodiscard]] OverlaySessionData const &State() const noexcept { return state_; }

    void Set_final_selection(RectPx r);

    void Push_command(std::unique_ptr<ICommand> cmd);
    void Undo();
    void Redo();

    [[nodiscard]] OverlayAction On_annotation_tool_hotkey(wchar_t hotkey);
    [[nodiscard]] OverlayAction On_select_annotation_tool(AnnotationToolId id);
    [[nodiscard]] OverlayAction On_delete_selected_annotation();
    [[nodiscard]] std::vector<AnnotationToolbarButtonView>
    Build_annotation_toolbar_button_views() const;
    [[nodiscard]] std::span<const Annotation> Annotations() const noexcept;
    [[nodiscard]] Annotation const *Draft_annotation() const noexcept;
    [[nodiscard]] std::span<const PointPx> Draft_freehand_points() const noexcept;
    [[nodiscard]] std::optional<StrokeStyle> Draft_freehand_style() const noexcept;
    [[nodiscard]] std::optional<double> Draft_line_angle_radians() const noexcept;
    [[nodiscard]] Annotation const *Selected_annotation() const noexcept;
    [[nodiscard]] std::optional<RectPx> Selected_annotation_bounds() const noexcept;
    [[nodiscard]] std::optional<AnnotationEditTarget>
    Annotation_edit_target_at(PointPx cursor) const noexcept;
    [[nodiscard]] std::optional<AnnotationToolId>
    Active_annotation_tool() const noexcept;
    [[nodiscard]] bool Has_active_annotation_edit() const noexcept;
    [[nodiscard]] std::optional<AnnotationEditHandleKind>
    Active_annotation_edit_handle() const noexcept;
    [[nodiscard]] int32_t Brush_width_px() const noexcept;
    [[nodiscard]] COLORREF Annotation_color() const noexcept;
    void Set_brush_width_px(int32_t width_px) noexcept;
    void Set_annotation_color(COLORREF color) noexcept;
    [[nodiscard]] std::optional<int32_t> Adjust_brush_width(int32_t delta_steps);
    [[nodiscard]] bool Should_show_annotation_toolbar() const noexcept;
    [[nodiscard]] bool Can_interact_with_annotation_toolbar() const noexcept;
    [[nodiscard]] bool Should_show_selected_annotation_handles() const noexcept;
    [[nodiscard]] bool Has_active_annotation_gesture() const noexcept;
    [[nodiscard]] bool Is_annotation_dragging() const noexcept;
    [[nodiscard]] bool Has_annotation_at(PointPx cursor) const noexcept;

  private:
    void Rebuild_snap_edges(SnapEdges const &screen_edges, int32_t origin_x,
                            int32_t origin_y);
    void Apply_modifier_preview(OverlayModifierState mods, PointPx cursor_screen,
                                std::optional<RectPx> window_rect_screen,
                                RectPx virtual_desktop_bounds,
                                std::optional<size_t> monitor_index, int32_t origin_x,
                                int32_t origin_y);

    OverlaySessionData state_;
    UndoStack undo_stack_;
    AnnotationController annotation_controller_;
};

} // namespace greenflame::core
