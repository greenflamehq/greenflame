#pragma once

// Direct2D overlay paint pipeline.

#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/freehand_smoothing.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/selection_handles.h"
#include "greenflame_core/selection_wheel.h"
#include "greenflame_core/text_annotation_types.h"

namespace greenflame {

// Length of the repeating color pattern for the selected-annotation marquee, in pixels.
inline constexpr int32_t kSelectedAnnotationMarqueePatternLengthPx = 8;

struct D2DOverlayResources;
class IOverlayButton;
class IOverlayTopLayer;

// Per-index override used by the obfuscate preview path to avoid a full annotation
// deep-copy — only the changed indices are patched at render time.
struct AnnotationPreviewPatch {
    size_t index;
    core::Annotation annotation;
};

// Per-frame paint input for the Direct2D overlay renderer.
struct D2DPaintInput {
    ID2D1Bitmap *lifted_window_bitmap = nullptr;
    core::Annotation const *draft_annotation = nullptr;
    core::TextAnnotation const *draft_text_annotation = nullptr;
    core::Annotation const *selected_annotation = nullptr;
    std::optional<core::RectPx> selected_annotation_bounds = std::nullopt;
    size_t selection_wheel_segment_count = 0;
    std::span<const core::RectPx> monitor_rects_client = {};
    std::span<const core::Annotation> annotations = {};
    std::span<const AnnotationPreviewPatch> annotation_patches = {};
    std::span<const core::PointPx> draft_freehand_points = {};
    std::wstring_view transient_center_label_text = {};
    std::span<IOverlayButton *const> toolbar_buttons = {};
    // Parallel to toolbar_buttons: D2D glyph bitmap for each button (may be null).
    std::span<ID2D1Bitmap *const> toolbar_button_glyphs = {};
    std::wstring_view toolbar_tooltip_text = {};
    std::span<const COLORREF> selection_wheel_colors = {};
    std::optional<size_t> selection_wheel_selected_segment = std::nullopt;
    std::optional<size_t> selection_wheel_hovered_segment = std::nullopt;
    std::optional<core::SizePx> selection_size_override = std::nullopt;
    std::wstring_view text_wheel_hub_font_family = {};
    std::vector<core::RectPx> draft_text_selection_rects = {};
    std::optional<core::TextAnnotationBaseStyle> text_cursor_preview_style =
        std::nullopt;
    std::array<std::wstring_view, 4> selection_wheel_font_families = {};
    float draft_freehand_blit_opacity = 1.0f;
    int32_t highlighter_wheel_current_opacity_percent = 0;
    int32_t selected_annotation_marquee_phase_px = 0;
    COLORREF highlighter_wheel_current_color = 0;
    // Ring layout for clamped-nav wheels: the last segment is a phantom (not drawn).
    float selection_wheel_ring_angle_offset = 0.0f;
    core::PointPx cursor_client_px = {};
    std::optional<int32_t> brush_cursor_preview_width_px = std::nullopt;
    std::optional<int32_t> square_cursor_preview_width_px = std::nullopt;
    std::optional<int32_t> arrow_cursor_preview_width_px = std::nullopt;
    core::PointPx selection_wheel_center_px = {};
    core::RectPx live_rect = {};
    core::RectPx annotation_selection_live_rect = {};
    core::RectPx final_selection = {};
    core::RectPx lifted_window_dest_rect = {};
    core::RectPx lifted_window_source_rect = {};
    core::RectPx draft_text_caret_rect = {};
    std::optional<core::StrokeStyle> draft_freehand_style = std::nullopt;
    core::FreehandSmoothingMode draft_freehand_smoothing_mode =
        core::FreehandSmoothingMode::Off;
    std::optional<core::RectPx> hovered_toolbar_bounds = std::nullopt;
    bool draft_text_insert_mode = true;
    bool draft_text_blink_visible = true;
    core::FreehandTipShape draft_freehand_tip_shape = core::FreehandTipShape::Round;
    bool dragging = false;
    bool handle_dragging = false;
    bool move_dragging = false;
    bool annotation_selection_dragging = false;
    bool annotation_editing = false; // annotation endpoint/resize/translate in progress
    bool modifier_preview = false;
    bool show_selection_size_side_labels = true;
    bool show_selection_size_center_label = true;
    bool show_selection_wheel = false;
    bool selection_wheel_has_style_hub = false;
    core::TextWheelMode text_wheel_active_mode = core::TextWheelMode::Color;
    bool selection_wheel_has_highlighter_hub = false;
    core::HighlighterWheelMode highlighter_wheel_active_mode =
        core::HighlighterWheelMode::Color;
    bool selection_wheel_clamp_nav = false;
    bool show_offscreen_capture_note = false;
    std::optional<core::SelectionHandle> highlight_handle = std::nullopt;
    std::optional<core::TextWheelHubSide> text_wheel_hovered_hub = std::nullopt;
    std::optional<core::HighlighterWheelHubSide> highlighter_wheel_hovered_hub =
        std::nullopt;
};

// Rebuild the annotations off-screen bitmap from committed annotations.
// Sets res.annotations_valid = true on success.
// patches: optional per-index overrides for the obfuscate preview path.
void Rebuild_annotations_bitmap(D2DOverlayResources &res,
                                std::span<const core::Annotation> annotations,
                                std::span<const AnnotationPreviewPatch> patches = {},
                                std::optional<uint64_t> skip_id = std::nullopt);

// Rebuild the frozen off-screen bitmap: screenshot + dim + selection restore +
// annotations. Sets res.frozen_valid = true on success.
void Rebuild_frozen_bitmap(D2DOverlayResources &res, core::RectPx selection,
                           int vd_width, int vd_height);

// Draw one complete frame. Returns false on D2DERR_RECREATE_TARGET (device lost).
// If top_layer is non-null and visible, it is drawn on top of everything else within
// the same BeginDraw/EndDraw pair.
[[nodiscard]] bool Paint_d2d_frame(D2DOverlayResources &res, D2DPaintInput const &input,
                                   int vd_width, int vd_height,
                                   IOverlayTopLayer *top_layer = nullptr);

} // namespace greenflame
