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
    // Flip-model swap chain stack. Replaces ID2D1HwndRenderTarget so that
    // Present() no longer falls into CDXGISwapChain::PrepareWindowedBltPresent
    // (the legacy DWM-throttled blit path that blocked ~28 ms per frame on the
    // overlay's WM_PAINT thread). With FRAME_LATENCY_WAITABLE_OBJECT and a max
    // latency of 1, the message pump throttles itself by waiting on the
    // waitable handle before producing a frame, instead of stalling inside
    // Present.
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swap_chain;
    Microsoft::WRL::ComPtr<ID2D1Device> d2d_device;
    // The "hwnd_rt" name is kept for blast-radius reasons: every Draw_*
    // helper and offscreen-rt creator already takes an ID2D1RenderTarget*.
    // ID2D1DeviceContext IS-A ID2D1RenderTarget so existing callsites keep
    // working unchanged.
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> hwnd_rt;
    // The render target bitmap. With a swap chain (Create_hwnd_rt) it wraps
    // swap_chain->GetBuffer(0), made once at swap-chain creation; FLIP_DISCARD with
    // BufferCount==2 keeps the same surface object across Present() calls. With
    // Create_offscreen_target (render benchmark) it is a plain offscreen bitmap.
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> back_buffer_bitmap;
    // Frame-latency waitable. Wait on this BEFORE BeginDraw so the paint
    // pipeline never builds frames faster than DWM consumes them.
    HANDLE frame_latency_waitable = nullptr;
    float target_dpi = kDefaultTargetDpi;
    // ArithmeticComposite effect (k1=1, k2=k3=k4=0) for multiply-blend highlighting.
    // Created by Create_device; null if effect creation failed.
    Microsoft::WRL::ComPtr<ID2D1Effect> multiply_effect;
    // Composite effect used to merge cached highlighter body + live tail masks before
    // the multiply pass. Created by Create_device; null if effect creation failed.
    Microsoft::WRL::ComPtr<ID2D1Effect> draft_stroke_composite_effect;
    // Composite effect that combines the screenshot with already-committed annotations
    // to form the base (input 0) of the highlighter multiply. This is what lets a
    // highlighter tint the pixels of prior annotations that sit beneath it, rather
    // than only tinting the raw screenshot.
    Microsoft::WRL::ComPtr<ID2D1Effect> base_composite_effect;

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
    // Snapshot of annotations_rt contents taken mid-rebuild, just before a highlighter
    // draws. Used as input 1 of base_composite_effect so the multiply sees the prior
    // annotations underneath.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> base_composite_bitmap;
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

    // Live brush (round-tip) stroke. The smoother turns the stroke's stable body into
    // final points, drawn once each into draft_stroke_body_rt (append only, at full
    // opacity, max blend). draft_stroke_rt holds live_base: the whole overlay frame
    // (screenshot, annotations, body, dim) as of the last body update, so a frame is
    // one live_base blit plus a recomposite clipped to the moving tail.
    struct RoundDraftState final {
        bool active = false;
        core::FreehandIncrementalSmoother smoother = {};
        core::StrokeStyle style = {};
        core::PointPx first_point = {};
        size_t raw_point_count = 0;    // raw stroke points seen
        size_t fed_point_count = 0;    // raw points fed to the smoother
        size_t drawn_final_count = 0;  // final points drawn into the body bitmap
        core::RectPx body_bounds = {}; // pixels the body bitmap covers
        bool live_base_valid = false;
        core::RectPx live_base_restore_rect = {};
        ID2D1Bitmap *live_base_lifted_window = nullptr;
    };
    RoundDraftState round_draft = {};

    // Reusable shared resources (recreated on device loss)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solid_brush;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> round_cap_style; // freehand, lines
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> flat_cap_style;  // rectangles
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> dashed_style;    // selection border
    // 1-on/1-off stipple bitmap brush for the fullscreen crosshair guides.
    // Replaces the prior CreateStrokeStyle dashed style: a custom 2-DIP dash
    // explodes into one convex figure per dash inside D2D's tessellator
    // (CDasher -> CWidener -> CConvexFigureStorage), which dominated CPU on
    // 4K-class virtual desktops where each crosshair line spans thousands of
    // dashes per frame. A 2x2 wrapping bitmap brush samples to the same
    // 1-on/1-off pixel pattern with zero tessellation.
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> crosshair_stipple_brush;
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

    // Create (or recreate) the D3D11 device, the D2D device context (hwnd_rt) and
    // the shared effects. Call after Initialize_factory. Pass a null adapter with
    // HARDWARE or WARP, or a real adapter with UNKNOWN.
    [[nodiscard]] bool Create_device(IDXGIAdapter *adapter,
                                     D3D_DRIVER_TYPE driver_type);

    // Create (or recreate) the device and a swap chain on hwnd. A null adapter means
    // the default hardware adapter (the app always passes null). Call after
    // Initialize_factory.
    [[nodiscard]] bool Create_hwnd_rt(HWND hwnd, int width, int height,
                                      IDXGIAdapter *adapter = nullptr);

    // Target an offscreen bitmap the size of the virtual desktop instead of a swap
    // chain (render benchmark). Call after Create_device.
    [[nodiscard]] bool Create_offscreen_target(int width, int height);

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
