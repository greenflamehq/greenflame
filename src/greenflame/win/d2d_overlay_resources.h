#pragma once

#include "greenflame/win/gdi_capture.h"
#include "greenflame/win/overlay_button.h"

namespace greenflame {

// Holds all Direct2D and DirectWrite resources for the overlay render pipeline.
// Lifetime: created once per overlay session; released when the overlay closes.
//
// Layer model:
//   screenshot    — uploaded once at capture time, never redrawn
//   annotations   — rebuilt on annotation commit/undo/redo/delete
//   frozen        — rebuilt when selection or annotations change
//   draft_stroke  — incrementally updated during freehand gesture (O(1) per frame)
//   live layer    — drawn every frame (draft blit, selection border, handles, UI)
struct D2DOverlayResources final {
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> hwnd_rt;

    // Per-session bitmaps
    Microsoft::WRL::ComPtr<ID2D1Bitmap> screenshot;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> annotations_rt;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> frozen_rt;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> draft_stroke_rt;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> annotations_bitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> frozen_bitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> draft_stroke_bitmap;
    size_t draft_stroke_point_count = 0; // points rendered into draft_stroke_rt

    // Reusable shared resources (recreated on device loss)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solid_brush;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> round_cap_style; // freehand, lines
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> flat_cap_style;  // rectangles
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> dashed_style;    // selection border
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle>
        crosshair_style;                                   // 1-on/1-off for crosshair
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_dim;    // 14pt Segoe UI
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_center; // 36pt Segoe UI Black
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_hint;   // 16pt Segoe UI

    // Toolbar glyph bitmaps (BGRA premultiplied, from OverlayButtonGlyph alpha masks)
    Microsoft::WRL::ComPtr<ID2D1Bitmap> glyph_brush;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> glyph_line;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> glyph_arrow;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> glyph_rect;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> glyph_filled_rect;

    bool annotations_valid = false;
    bool frozen_valid = false;

    // Initialize the process-lifetime factories (call once).
    [[nodiscard]] bool Initialize_factory();

    // Create (or recreate) the HwndRenderTarget. Call after Initialize_factory.
    [[nodiscard]] bool Create_hwnd_rt(HWND hwnd, int width, int height);

    // Upload the GDI capture as a D2D bitmap.
    [[nodiscard]] bool Upload_screenshot(GdiCaptureResult const &cap);

    // Create device-dependent shared resources (brushes, stroke styles, text formats).
    [[nodiscard]] bool Create_shared_resources();

    // Create the annotations and frozen off-screen bitmap render targets.
    [[nodiscard]] bool Create_cache_targets(int width, int height);

    // Upload the five annotation tool glyph alpha masks as D2D bitmaps.
    // Order: brush, line, arrow, rect, filled_rect. Null pointers are skipped.
    [[nodiscard]] bool Upload_glyph_bitmaps(OverlayButtonGlyph const *brush,
                                            OverlayButtonGlyph const *line,
                                            OverlayButtonGlyph const *arrow,
                                            OverlayButtonGlyph const *rect,
                                            OverlayButtonGlyph const *filled_rect);

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
