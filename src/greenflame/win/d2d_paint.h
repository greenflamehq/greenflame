#pragma once

// Direct2D overlay paint pipeline.

#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/color_wheel.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/selection_handles.h"

namespace greenflame {

struct D2DOverlayResources;
class IOverlayButton;
class OverlayHelpOverlay;

// Per-frame paint input for the Direct2D overlay renderer.
struct D2DPaintInput {
    core::RectPx live_rect = {};
    core::RectPx final_selection = {};
    core::PointPx cursor_client_px = {};
    std::span<const core::RectPx> monitor_rects_client = {};
    std::span<const core::Annotation> annotations = {};
    core::Annotation const *draft_annotation = nullptr;
    core::TextAnnotation const *draft_text_annotation = nullptr;
    std::vector<core::RectPx> draft_text_selection_rects = {};
    core::RectPx draft_text_caret_rect = {};
    bool draft_text_insert_mode = true;
    bool draft_text_blink_visible = true;
    std::span<const core::PointPx> draft_freehand_points = {};
    std::optional<core::StrokeStyle> draft_freehand_style = std::nullopt;
    core::FreehandTipShape draft_freehand_tip_shape = core::FreehandTipShape::Round;
    float draft_freehand_blit_opacity = 1.0f;
    std::optional<int32_t> brush_cursor_preview_width_px = std::nullopt;
    std::optional<int32_t> square_cursor_preview_width_px = std::nullopt;
    std::optional<double> square_cursor_preview_angle_radians = std::nullopt;
    bool dragging = false;
    bool handle_dragging = false;
    bool move_dragging = false;
    bool annotation_editing = false; // annotation endpoint/resize/translate in progress
    bool modifier_preview = false;
    bool show_selection_size_side_labels = true;
    bool show_selection_size_center_label = true;
    std::optional<core::SelectionHandle> highlight_handle = std::nullopt;
    core::Annotation const *selected_annotation = nullptr;
    std::optional<core::RectPx> selected_annotation_bounds = std::nullopt;
    std::wstring_view transient_center_label_text = {};
    std::span<IOverlayButton *const> toolbar_buttons = {};
    // Parallel to toolbar_buttons: D2D glyph bitmap for each button (may be null).
    std::span<ID2D1Bitmap *const> toolbar_button_glyphs = {};
    std::wstring_view toolbar_tooltip_text = {};
    std::optional<core::RectPx> hovered_toolbar_bounds = std::nullopt;
    bool show_color_wheel = false;
    core::PointPx color_wheel_center_px = {};
    std::span<const COLORREF> color_wheel_colors = {};
    size_t color_wheel_segment_count = 0;
    std::optional<size_t> color_wheel_selected_segment = std::nullopt;
    std::optional<size_t> color_wheel_hovered_segment = std::nullopt;
    bool color_wheel_has_style_hub = false;
    core::TextWheelMode text_wheel_active_mode = core::TextWheelMode::Color;
    std::optional<core::TextWheelHubSide> text_wheel_hovered_hub = std::nullopt;
    std::wstring_view text_wheel_hub_font_family = {};
    std::array<std::wstring_view, 4> color_wheel_font_families = {};
};

// Rebuild the annotations off-screen bitmap from committed annotations.
// Sets res.annotations_valid = true on success.
void Rebuild_annotations_bitmap(D2DOverlayResources &res,
                                std::span<const core::Annotation> annotations);

// Rebuild the frozen off-screen bitmap: screenshot + dim + selection restore +
// annotations. Sets res.frozen_valid = true on success.
void Rebuild_frozen_bitmap(D2DOverlayResources &res, core::RectPx selection,
                           int vd_width, int vd_height);

// Draw one complete frame. Returns false on D2DERR_RECREATE_TARGET (device lost).
// If help_overlay is non-null and visible, it is drawn on top of everything else within
// the same BeginDraw/EndDraw pair.
[[nodiscard]] bool Paint_d2d_frame(D2DOverlayResources &res, D2DPaintInput const &input,
                                   int vd_width, int vd_height,
                                   OverlayHelpOverlay *help_overlay = nullptr);

} // namespace greenflame
