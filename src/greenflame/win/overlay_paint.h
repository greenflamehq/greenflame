#pragma once

// Overlay painting: capture blit, selection dim/border, dimension labels,
// crosshair, round magnifier, coord tooltip, contour handles. Caller provides
// read-only paint input; no dependency on OverlayState or window state
// accessors.

#include "greenflame_core/rect_px.h"

namespace greenflame {

struct GdiCaptureResult;

// Cached GDI resources created once at overlay init, reused every frame.
struct PaintResources {
    HFONT font_dim = nullptr;       // 14pt "Segoe UI" normal
    HFONT font_center = nullptr;    // 36pt "Segoe UI" black weight
    HFONT font_help_hint = nullptr; // 16pt "Segoe UI" normal
    HPEN crosshair_pen = nullptr;
    HPEN border_pen = nullptr;
    HPEN handle_pen = nullptr;
};

namespace core {
enum class SelectionHandle : uint8_t;
} // namespace core

class IOverlayButton;

struct PaintOverlayInput {
    GdiCaptureResult const *capture = nullptr;
    core::RectPx live_rect = {};
    core::RectPx final_selection = {};
    core::PointPx cursor_client_px = {};
    std::span<const core::RectPx> monitor_rects_client = {};
    std::span<uint8_t> paint_buffer = {};
    PaintResources const *resources = nullptr;
    bool dragging = false;
    bool handle_dragging = false;
    bool move_dragging = false;
    bool modifier_preview = false; // Shift/Ctrl: live_rect = window or monitor
    bool show_selection_size_side_labels = true;
    bool show_selection_size_center_label = true;
    std::optional<core::SelectionHandle> highlight_handle = std::nullopt;
    std::span<IOverlayButton *const> toolbar_buttons = {};
};

void Paint_overlay(HDC hdc, HWND hwnd, const RECT &rc, const PaintOverlayInput &in);

} // namespace greenflame
