#pragma once

#include "greenflame/win/gdi_capture.h"
#include "greenflame/win/overlay_button.h"
#include "greenflame_core/annotation_types.h"
#include "greenflame_core/freehand_smoothing.h"
#include "greenflame_core/text_annotation_types.h"

namespace greenflame::core {
enum class OverlayAction : uint8_t;
} // namespace greenflame::core

namespace greenflame {

// The selection border dash pattern while dragging, in pixels at stroke width 1.
inline constexpr float kBorderDashOnPx = 4.f;
inline constexpr float kBorderDashOffPx = 3.f;

// A lifted window capture as the overlay shows it: `bitmap`'s `source_rect` drawn 1:1
// at `dest_rect`. All null or empty when no window is lifted.
struct LiftedWindowView final {
    ID2D1Bitmap *bitmap = nullptr;
    core::RectPx dest_rect = {};
    core::RectPx source_rect = {};

    [[nodiscard]] bool Is_shown() const noexcept {
        return bitmap != nullptr && !dest_rect.Is_empty() && !source_rect.Is_empty();
    }
    bool operator==(LiftedWindowView const &) const noexcept = default;
};

// One surface a frame is drawn into: a window's flip-model swap chain, or an offscreen
// bitmap (render benchmark). It shows the client pixels inside rect_client, so the
// frame draws into it translated by minus rect_client's top-left.
struct D2DSurfaceTarget final {
    core::RectPx rect_client = {};
    // Null for an offscreen target.
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swap_chain;
    // The swap chain's back buffer (made once: FLIP_DISCARD with BufferCount 2 keeps
    // the same surface object across Present calls), or the offscreen bitmap.
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
    // Frame-latency waitable (swap chain only). Waited on before drawing so frames
    // are never built faster than DWM consumes them.
    HANDLE frame_latency_waitable = nullptr;
};

// Holds all Direct2D and DirectWrite resources for the overlay render pipeline.
// Lifetime: created once per overlay session; released when the overlay closes.
//
// Layer model:
//   screenshot    — uploaded once at capture time, never redrawn
//   annotations   — rebuilt on annotation commit/undo/redo/delete
//   frozen        — rebuilt when selection or annotations change; built for no
//                   selection (all dimmed) during a selection drag, resize or move
//   draft_stroke  — during a freehand stroke: the stroke body (append only) and
//                   live_base, the whole frame as of the last body update
//   live layer    — drawn every frame (selection border, handles, UI)
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
    Microsoft::WRL::ComPtr<ID2D1Device> d2d_device;
    // The "hwnd_rt" name is kept for blast-radius reasons: every Draw_*
    // helper and offscreen-rt creator already takes an ID2D1RenderTarget*.
    // ID2D1DeviceContext IS-A ID2D1RenderTarget so existing callsites keep
    // working unchanged.
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> hwnd_rt;
    // The surfaces each frame is drawn into, all on this device. The caches below are
    // desktop-sized and shared by every target.
    std::vector<D2DSurfaceTarget> targets;
    float target_dpi = kDefaultTargetDpi;
    // ArithmeticComposite effect (k2=k3=k4=0) for multiply-blend highlighting: input 0
    // is the base, input 1 the highlighter mask, k1 the opacity (1 when the mask
    // carries it). Created by Create_device; null if effect creation failed.
    Microsoft::WRL::ComPtr<ID2D1Effect> multiply_effect;
    // Composite effect that combines the screenshot with already-committed annotations
    // to form the base (input 0) of the highlighter multiply. This is what lets a
    // highlighter tint the pixels of prior annotations that sit beneath it, rather
    // than only tinting the raw screenshot.
    Microsoft::WRL::ComPtr<ID2D1Effect> base_composite_effect;
    // A lifted window shows its capture, not the screenshot, so highlighters over it
    // multiply onto the capture: the capture moved to its dest rect (affine
    // transform), cut to it (crop), over the screenshot (composite).
    Microsoft::WRL::ComPtr<ID2D1Effect> lifted_transform_effect;
    Microsoft::WRL::ComPtr<ID2D1Effect> lifted_crop_effect;
    Microsoft::WRL::ComPtr<ID2D1Effect> lifted_base_effect;

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
    // Annotation edits: annotations_bitmap as of the annotations below the edit (see
    // edit_cache), copied back at the start of each edit frame's rebuild.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> edit_below_bitmap;

    // Live freehand stroke (brush and highlighter). The smoother turns the stroke's
    // stable body into final points, drawn once each into draft_stroke_body_rt (append
    // only, at full opacity, max blend). draft_stroke_rt holds live_base: the whole
    // overlay frame (screenshot, annotations, body, dim) as of the last body update, so
    // a frame is one live_base blit plus a recomposite clipped to the moving tail. The
    // brush composites the body under a layer at its opacity; the highlighter
    // multiplies by it as an opaque mask, with the opacity as k1.
    struct FreehandDraftState final {
        bool active = false;
        core::FreehandIncrementalSmoother smoother = {};
        core::StrokeStyle style = {};
        core::FreehandTipShape tip_shape = core::FreehandTipShape::Round;
        int32_t blit_opacity_percent = core::StrokeStyle::kMaxOpacityPercent;
        core::PointPx first_point = {};
        size_t raw_point_count = 0;    // raw stroke points seen
        size_t fed_point_count = 0;    // raw points fed to the smoother
        size_t drawn_final_count = 0;  // final points drawn into the body bitmap
        core::RectPx body_bounds = {}; // pixels the body bitmap covers
        bool live_base_valid = false;
        core::RectPx live_base_restore_rect = {};
        ID2D1Bitmap *live_base_lifted_window = nullptr;
    };
    FreehandDraftState freehand_draft = {};

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
    // The last few texts measured for overlay labels, so a frame drawn into several
    // per-monitor targets lays each label out once, not once per target. Cleared with
    // the text formats it is keyed by.
    struct MeasuredText final {
        IDWriteTextFormat *format = nullptr;
        std::wstring text = {};
        float width = 0.f;
        float height = 0.f;
    };
    static constexpr size_t kMeasuredTextSlots = 4;
    std::array<MeasuredText, kMeasuredTextSlots> measured_text = {};
    size_t measured_text_next = 0;
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
    // The annotations annotations_bitmap was last fully built from (no preview patches,
    // no skipped annotation), and the ones frozen_bitmap shows. Null when not known,
    // as after an edit frame's rebuild.
    std::shared_ptr<const std::vector<core::Annotation>> annotations_source = nullptr;
    std::shared_ptr<const std::vector<core::Annotation>> frozen_source = nullptr;
    // Annotation edits (move, resize). Every edit frame rebuilds annotations_bitmap.
    // The annotations below the lowest one the edit changes stay the same, so they are
    // drawn once into edit_below_bitmap and each frame draws only the rest.
    struct EditCache final {
        bool valid = false;
        std::vector<core::Annotation> below = {}; // what edit_below_bitmap holds
        // Whether frozen_bitmap shows the annotations the edit started from, so a frame
        // can recomposite only what the edit changed. Decided at the edit's first
        // frame; cleared when frozen_bitmap is rebuilt and when the edit ends.
        std::optional<bool> frozen_is_edit_start = std::nullopt;
    };
    EditCache edit_cache = {};
    // False: edit frames take the full rebuild and the dynamic path (render bench
    // checks against it).
    bool edit_fast_paths = true;
    // How often the edit fast paths ran: frames drawn as a changed-area recomposite,
    // and frames whose rebuild reused edit_below_bitmap. The render bench checks them.
    struct EditStats final {
        size_t region_frames = 0;
        size_t below_cache_hits = 0;
    };
    EditStats edit_stats = {};
    // True when annotations_bitmap was last built from no annotations, so it is all
    // transparent and blitting it changes nothing.
    bool annotations_empty = false;
    bool frozen_valid = false;
    core::RectPx frozen_selection = {}; // the selection frozen_bitmap was built with
    // QPC ticks the last Paint_d2d_frame spent in its frame-latency wait (debug log).
    int64_t last_frame_wait_qpc = 0;
    // The lifted window the overlay shows. Committed highlighters multiply onto it
    // inside its dest rect; set it with Set_lifted_view.
    LiftedWindowView lifted_view = {};

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

    // Create (or recreate) the device and one swap-chain target on hwnd covering
    // client pixels (0, 0)-(width, height). A null adapter means the default hardware
    // adapter (the app always passes null). Call after Initialize_factory.
    [[nodiscard]] bool Create_hwnd_rt(HWND hwnd, int width, int height,
                                      IDXGIAdapter *adapter = nullptr);

    // Add a target: a swap chain on hwnd, on this device, sized to rect_client. Call
    // after Create_device.
    [[nodiscard]] bool Add_swap_chain_target(HWND hwnd, core::RectPx rect_client);

    // Add an offscreen target sized to rect_client (render benchmark). Call after
    // Create_device.
    [[nodiscard]] bool Add_offscreen_target(core::RectPx rect_client);

    // Release every target (swap chains, bitmaps, waitables).
    void Release_targets() noexcept;

    // Upload the GDI capture as a D2D bitmap.
    [[nodiscard]] bool Upload_screenshot(GdiCaptureResult const &cap);
    [[nodiscard]] bool Upload_lifted_window_capture(GdiCaptureResult const &cap);
    void Clear_lifted_window_capture() noexcept;
    // Sets lifted_view; a change marks annotations (and frozen) stale.
    void Set_lifted_view(LiftedWindowView const &view) noexcept;

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

    // Marks the caches an overlay controller action makes stale. The window and the
    // render bench both apply actions through this.
    void Apply_overlay_action(core::OverlayAction action) noexcept;

    // Release all device-dependent resources except factory and dwrite_factory.
    void Release_device_resources();

    // Release everything including factories.
    void Release_all();
};

} // namespace greenflame
