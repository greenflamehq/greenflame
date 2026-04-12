#pragma once

#include "greenflame/win/gdi_capture.h"
#include "greenflame/win/overlay_button.h"
#include "greenflame_core/annotation_types.h"
#include "greenflame_core/freehand_smoothing.h"
#include "greenflame_core/text_annotation_types.h"

namespace greenflame {

// Holds all Direct2D and DirectWrite resources for the overlay render pipeline.
// Lifetime: created once per overlay session; released when the overlay closes.
//
// Layer model:
//   screenshot    — uploaded once at capture time, never redrawn
//   annotations   — rebuilt on annotation commit/undo/redo/delete
//   frozen        — rebuilt when selection or annotations change
//   draft_stroke  — rebuilt during freehand gesture from raw or split-tail preview
//                   segments, depending on the active smoothing mode
//   live layer    — drawn every frame (draft blit, selection border, handles, UI)
struct D2DOverlayResources final {
    static constexpr float kDefaultTargetDpi = 96.f;
    Microsoft::WRL::ComPtr<ID2D1Factory1> factory;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> hwnd_rt;
    float target_dpi = kDefaultTargetDpi;
    // ArithmeticComposite effect (k1=1, k2=k3=k4=0) for multiply-blend highlighting.
    // Null until Create_hwnd_rt succeeds and ID2D1DeviceContext QI is available.
    Microsoft::WRL::ComPtr<ID2D1Effect> multiply_effect;
    // Composite effect used to merge cached highlighter body + live tail masks before
    // the multiply pass. Null until Create_hwnd_rt succeeds and ID2D1DeviceContext QI
    // is available.
    Microsoft::WRL::ComPtr<ID2D1Effect> draft_stroke_composite_effect;

    // Per-session bitmaps
    Microsoft::WRL::ComPtr<ID2D1Bitmap> screenshot;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> lifted_window_capture;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> annotations_rt;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> frozen_rt;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> draft_stroke_rt;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> draft_stroke_body_rt;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> annotations_bitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> frozen_bitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> draft_stroke_bitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> draft_stroke_body_bitmap;
    size_t draft_stroke_point_count = 0; // points rendered into draft_stroke_rt
    core::PointPx draft_stroke_last_point =
        {}; // last point rendered into draft_stroke_rt
    size_t draft_stroke_body_raw_point_count = 0;
    size_t draft_stroke_body_point_count = 0;
    size_t draft_stroke_stable_tail_start_index = 0;
    std::optional<core::StrokeStyle> draft_stroke_style = std::nullopt;
    core::FreehandTipShape draft_stroke_tip_shape = core::FreehandTipShape::Round;
    core::FreehandSmoothingMode draft_stroke_smoothing_mode =
        core::FreehandSmoothingMode::Off;
    bool draft_stroke_bitmap_uses_cached_body = false;

    // Reusable shared resources (recreated on device loss)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solid_brush;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> round_cap_style; // freehand, lines
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> flat_cap_style;  // rectangles
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> dashed_style;    // selection border
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle>
        crosshair_style;                                   // 1-on/1-off for crosshair
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_dim;    // 12pt Segoe UI
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_center; // 36pt Segoe UI Black
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_hint;   // 16pt Segoe UI
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> text_wheel_hue_brush;
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> checker_brush;

    // Toolbar glyph bitmaps (BGRA premultiplied, from OverlayButtonGlyph alpha
    // masks), indexed by OverlayToolbarGlyphId.
    std::array<Microsoft::WRL::ComPtr<ID2D1Bitmap>,
               static_cast<size_t>(OverlayToolbarGlyphId::Count)>
        toolbar_glyphs = {};

    // Per-annotation bitmap caches: keyed by annotation ID.
    // Cleared on Invalidate_annotations() and Release_device_resources().
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID2D1Bitmap>> obfuscate_bitmaps;
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID2D1Bitmap>> text_bitmaps;
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID2D1Bitmap>> bubble_bitmaps;

    // Cached glyph outline geometry for the text cursor preview ("A").
    // Device-independent (path geometry from factory); cleared only in Release_all().
    // The geometries are built at layout origin (0,0) with y-flip applied;
    // a render-target translation to the cursor position is pushed at draw time.
    struct TextCursorPreviewCache {
        core::TextFontChoice font_choice = core::TextFontChoice::Sans;
        std::wstring font_family = {};
        int32_t point_size = 0;
        std::vector<Microsoft::WRL::ComPtr<ID2D1TransformedGeometry>> geometries;
    };
    std::optional<TextCursorPreviewCache> text_cursor_preview_cache;

    bool annotations_valid = false;
    bool frozen_valid = false;

    // Initialize the process-lifetime factories (call once).
    [[nodiscard]] bool Initialize_factory();

    // Set the render target DPI used for the overlay surface and uploaded bitmaps.
    void Set_target_dpi(float dpi) noexcept;
    [[nodiscard]] float Target_dpi() const noexcept;

    // Create (or recreate) the HwndRenderTarget. Call after Initialize_factory.
    [[nodiscard]] bool Create_hwnd_rt(HWND hwnd, int width, int height);

    // Upload the GDI capture as a D2D bitmap.
    [[nodiscard]] bool Upload_screenshot(GdiCaptureResult const &cap);
    [[nodiscard]] bool Upload_lifted_window_capture(GdiCaptureResult const &cap);
    void Clear_lifted_window_capture() noexcept;

    // Create device-dependent shared resources (brushes, stroke styles, text formats).
    [[nodiscard]] bool Create_shared_resources();

    // Create the annotations and frozen off-screen bitmap render targets.
    [[nodiscard]] bool Create_cache_targets(int width, int height);

    // Upload toolbar glyph alpha masks as D2D bitmaps, indexed by
    // OverlayToolbarGlyphId. Null entries are skipped.
    [[nodiscard]] bool
    Upload_glyph_bitmaps(std::span<OverlayButtonGlyph const *const> glyphs);
    [[nodiscard]] ID2D1Bitmap *
    Toolbar_glyph_bitmap(OverlayToolbarGlyphId glyph) const noexcept;

    // Mark annotations cache (and frozen cache) dirty.
    void Invalidate_annotations() noexcept;

    // Mark only the frozen cache dirty (annotations are still valid).
    void Invalidate_frozen() noexcept;

    // Release all device-dependent resources except factory and dwrite_factory.
    void Release_device_resources();

    // Release everything including factories.
    void Release_all();
};

} // namespace greenflame
