#pragma once

#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/save_image_policy.h"
#include "greenflame_core/selection_handles.h"

namespace greenflame::core {

struct OverlayModifierState {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool tab = false;
};

enum class OverlayAction : uint8_t {
    None,
    Repaint,
    Close,
    SaveDirect,            // Ctrl+S
    SaveDirectAndCopyFile, // Ctrl+Alt+S
    SaveAs,                // Ctrl+Shift+S
    SaveAsAndCopyFile,     // Ctrl+Shift+Alt+S
    CopyToClipboard,       // Ctrl+C
};

struct OverlaySessionData {
    bool dragging = false;
    bool handle_dragging = false;
    bool move_dragging = false;
    bool modifier_preview = false;
    std::optional<SelectionHandle> resize_handle = std::nullopt;
    RectPx resize_anchor_rect = {};
    PointPx move_grab_offset = {};
    RectPx move_anchor_rect = {};
    PointPx start_px = {};
    RectPx live_rect = {};
    RectPx final_selection = {};
    SaveSelectionSource selection_source = SaveSelectionSource::Region;
    std::optional<HWND> selection_window = std::nullopt;
    std::optional<size_t> selection_monitor_index = std::nullopt;
    uint64_t last_invalidate_tick = 0;
    std::vector<RectPx> window_rects = {};
    std::vector<int32_t> vertical_edges = {};
    std::vector<int32_t> horizontal_edges = {};
    std::vector<MonitorWithBounds> cached_monitors = {};

    void Reset_for_session();
};

class OverlayController final {
  public:
    void Reset_for_session(std::vector<MonitorWithBounds> monitors);

    // WM_LBUTTONDOWN: all Win32 queries are pre-resolved by caller.
    [[nodiscard]] OverlayAction On_primary_press(
        OverlayModifierState mods, PointPx cursor_client, PointPx cursor_screen,
        std::optional<HWND> window_under_cursor,
        std::optional<size_t> monitor_index_under_cursor,
        std::optional<RectPx> window_rect_screen, RectPx virtual_desktop_bounds,
        std::vector<RectPx> visible_window_rects, int32_t origin_x, int32_t origin_y);

    // WM_MOUSEMOVE
    [[nodiscard]] OverlayAction
    On_pointer_move(OverlayModifierState mods, PointPx cursor_client,
                    PointPx cursor_screen, std::optional<RectPx> window_rect_screen,
                    RectPx virtual_desktop_bounds,
                    std::optional<size_t> monitor_index_under_cursor, int32_t origin_x,
                    int32_t origin_y, uint64_t now_ms);

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

  private:
    void Rebuild_snap_edges(std::vector<RectPx> window_rects, int32_t origin_x,
                            int32_t origin_y);
    void Apply_modifier_preview(OverlayModifierState mods, PointPx cursor_screen,
                                std::optional<RectPx> window_rect_screen,
                                RectPx virtual_desktop_bounds,
                                std::optional<size_t> monitor_index, int32_t origin_x,
                                int32_t origin_y);

    OverlaySessionData state_;
};

} // namespace greenflame::core
