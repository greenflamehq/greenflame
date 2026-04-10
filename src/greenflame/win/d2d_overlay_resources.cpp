#include "greenflame/win/d2d_overlay_resources.h"

#include "greenflame/win/d2d_draw_helpers.h"

namespace greenflame {

namespace {

constexpr float kStrokeMiterLimit = 10.f;
constexpr float kDimTextFormatSizePt = 12.f;
constexpr float kCenterTextFormatSizePt = 27.f;
constexpr float kHintTextFormatSizePt = 12.f;
constexpr size_t kTextWheelHueStopCount = 7;
constexpr float kTextWheelHuePositionDivisor =
    static_cast<float>(kTextWheelHueStopCount - 1);
constexpr UINT32 kCheckerCellPx = 8;
constexpr UINT32 kCheckerBitmapPx = kCheckerCellPx * 2;
constexpr int kBytesPerPixel = 4;

// Convert an OverlayButtonGlyph alpha mask (single-channel, row-major) into an
// A8_UNORM D2D bitmap and store it in out_bitmap.
// D2D FillOpacityMask requires DXGI_FORMAT_A8_UNORM; any other format puts the
// render target into an error state and silently drops all subsequent draw calls.
// Returns false if the glyph is null/invalid or the upload fails.
[[nodiscard]] bool Upload_glyph(ID2D1RenderTarget *rt, OverlayButtonGlyph const *glyph,
                                Microsoft::WRL::ComPtr<ID2D1Bitmap> &out_bitmap) {
    if (!glyph || !glyph->Is_valid()) {
        out_bitmap.Reset();
        return true; // skip silently — caller may not provide all glyphs
    }

    int const w = glyph->width;
    int const h = glyph->height;

    D2D1_BITMAP_PROPERTIES props{};
    props.pixelFormat.format = DXGI_FORMAT_A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    float const dpi = Render_target_dpi(rt);
    props.dpiX = dpi;
    props.dpiY = dpi;

    UINT32 const row_bytes = static_cast<UINT32>(w); // 1 byte per pixel
    HRESULT const hr =
        rt->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)),
                         glyph->alpha_mask.data(), row_bytes, props,
                         out_bitmap.ReleaseAndGetAddressOf());
    return SUCCEEDED(hr);
}

[[nodiscard]] bool
Upload_capture_bitmap(ID2D1HwndRenderTarget *hwnd_rt, GdiCaptureResult const &cap,
                      float target_dpi,
                      Microsoft::WRL::ComPtr<ID2D1Bitmap> &out_bitmap) {
    if (hwnd_rt == nullptr || !cap.Is_valid()) {
        return false;
    }

    int const width = cap.width;
    int const height = cap.height;
    int const row_bytes =
        width > 0 && width <= (std::numeric_limits<int>::max() / kBytesPerPixel)
            ? width * kBytesPerPixel
            : 0;
    if (row_bytes <= 0 || height <= 0) {
        return false;
    }

    size_t const row_bytes_size = static_cast<size_t>(row_bytes);
    size_t const height_size = static_cast<size_t>(height);
    if (height_size > std::numeric_limits<size_t>::max() / row_bytes_size) {
        return false;
    }
    size_t const pixel_byte_count = row_bytes_size * height_size;

    std::vector<uint8_t> pixels = {};
    try {
        pixels.resize(pixel_byte_count);
    } catch (std::bad_alloc const &) {
        return false;
    }
    BITMAPINFOHEADER bmi{};
    bmi.biSize = sizeof(bmi);
    bmi.biWidth = width;
    bmi.biHeight = -height;
    bmi.biPlanes = 1;
    bmi.biBitCount = 32;
    bmi.biCompression = BI_RGB;

    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        return false;
    }
    int const lines =
        GetDIBits(screen_dc, cap.bitmap, 0, static_cast<UINT>(height), pixels.data(),
                  reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen_dc);
    if (lines != height) {
        return false;
    }

    for (size_t index = 3; index < pixels.size(); index += 4) {
        pixels[index] = 0xFF;
    }

    D2D1_BITMAP_PROPERTIES props{};
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    props.dpiX = target_dpi;
    props.dpiY = target_dpi;

    HRESULT const hr = hwnd_rt->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
        pixels.data(), static_cast<UINT32>(row_bytes), props,
        out_bitmap.ReleaseAndGetAddressOf());
    return SUCCEEDED(hr);
}

} // namespace

bool D2DOverlayResources::Initialize_factory() {
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr,
        reinterpret_cast<void **>(factory.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        return false;
    }

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown **>(dwrite_factory.ReleaseAndGetAddressOf()));
    return SUCCEEDED(hr);
}

void D2DOverlayResources::Set_target_dpi(float dpi) noexcept {
    target_dpi = dpi > 0.f ? dpi : kDefaultTargetDpi;
}

float D2DOverlayResources::Target_dpi() const noexcept { return target_dpi; }

bool D2DOverlayResources::Create_hwnd_rt(HWND hwnd, int width, int height) {
    if (!factory) {
        return false;
    }

    // The overlay surface still runs with a single target DPI for now. Later
    // per-monitor plumbing can set this before creating the HWND render target.
    D2D1_RENDER_TARGET_PROPERTIES rt_props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        Target_dpi(), Target_dpi());

    // RETAIN_CONTENTS keeps the previous frame visible to DWM during BeginDraw,
    // preventing the blank-surface flicker that occurs with PRESENT_OPTIONS_NONE.
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = D2D1::HwndRenderTargetProperties(
        hwnd, D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
        D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS);

    HRESULT const hr = factory->CreateHwndRenderTarget(
        rt_props, hwnd_props, hwnd_rt.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    // Create the ArithmeticComposite multiply-blend effect (O = I1 * I2).
    // Requires ID2D1DeviceContext, available when factory is ID2D1Factory1.
    multiply_effect.Reset();
    draft_stroke_composite_effect.Reset();
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc;
    if (SUCCEEDED(hwnd_rt->QueryInterface(IID_PPV_ARGS(&dc)))) {
        Microsoft::WRL::ComPtr<ID2D1Effect> effect;
        if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1ArithmeticComposite, &effect))) {
            // k1=1, k2=k3=k4=0 → O = I1 * I2 (premultiplied multiply).
            D2D1_VECTOR_4F const coeffs = {1.0f, 0.0f, 0.0f, 0.0f};
            (void)effect->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, coeffs);
            multiply_effect = std::move(effect);
        }
        if (SUCCEEDED(dc->CreateEffect(
                CLSID_D2D1Composite,
                draft_stroke_composite_effect.ReleaseAndGetAddressOf()))) {
            // Default mode is SOURCE_OVER, which is sufficient for combining
            // same-colored premultiplied coverage masks without seam darkening.
        }
    }
    return true;
}

bool D2DOverlayResources::Upload_screenshot(GdiCaptureResult const &cap) {
    return Upload_capture_bitmap(hwnd_rt.Get(), cap, Target_dpi(), screenshot);
}

bool D2DOverlayResources::Upload_lifted_window_capture(GdiCaptureResult const &cap) {
    return Upload_capture_bitmap(hwnd_rt.Get(), cap, Target_dpi(),
                                 lifted_window_capture);
}

void D2DOverlayResources::Clear_lifted_window_capture() noexcept {
    lifted_window_capture.Reset();
}

bool D2DOverlayResources::Create_shared_resources() {
    if (!hwnd_rt || !dwrite_factory) {
        return false;
    }

    HRESULT hr = hwnd_rt->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f),
                                                solid_brush.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    // Round cap style: for freehand strokes and lines.
    {
        D2D1_STROKE_STYLE_PROPERTIES props{};
        props.startCap = D2D1_CAP_STYLE_ROUND;
        props.endCap = D2D1_CAP_STYLE_ROUND;
        props.dashCap = D2D1_CAP_STYLE_ROUND;
        props.lineJoin = D2D1_LINE_JOIN_ROUND;
        props.miterLimit = kStrokeMiterLimit;
        props.dashStyle = D2D1_DASH_STYLE_SOLID;
        hr = factory->CreateStrokeStyle(props, nullptr, 0,
                                        round_cap_style.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    // Flat cap style: for rectangle outlines.
    {
        D2D1_STROKE_STYLE_PROPERTIES props{};
        props.startCap = D2D1_CAP_STYLE_FLAT;
        props.endCap = D2D1_CAP_STYLE_FLAT;
        props.dashCap = D2D1_CAP_STYLE_FLAT;
        props.lineJoin = D2D1_LINE_JOIN_MITER;
        props.miterLimit = kStrokeMiterLimit;
        props.dashStyle = D2D1_DASH_STYLE_SOLID;
        hr = factory->CreateStrokeStyle(props, nullptr, 0,
                                        flat_cap_style.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    // Dashed style: for selection border (4-on 3-off in DIPs at stroke width 1).
    {
        D2D1_STROKE_STYLE_PROPERTIES props{};
        props.startCap = D2D1_CAP_STYLE_FLAT;
        props.endCap = D2D1_CAP_STYLE_FLAT;
        props.dashCap = D2D1_CAP_STYLE_FLAT;
        props.lineJoin = D2D1_LINE_JOIN_MITER;
        props.miterLimit = kStrokeMiterLimit;
        props.dashStyle = D2D1_DASH_STYLE_CUSTOM;
        props.dashOffset = 0.f;
        float const dashes[] = {4.f, 3.f};
        hr = factory->CreateStrokeStyle(props, dashes, 2,
                                        dashed_style.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    // Crosshair style: 1px on, 1px off — matches GDI 1-on/1-off dotted crosshair.
    {
        D2D1_STROKE_STYLE_PROPERTIES props{};
        props.startCap = D2D1_CAP_STYLE_FLAT;
        props.endCap = D2D1_CAP_STYLE_FLAT;
        props.dashCap = D2D1_CAP_STYLE_FLAT;
        props.lineJoin = D2D1_LINE_JOIN_MITER;
        props.miterLimit = kStrokeMiterLimit;
        props.dashStyle = D2D1_DASH_STYLE_CUSTOM;
        props.dashOffset = 0.f;
        float const dashes[] = {1.f, 1.f};
        hr = factory->CreateStrokeStyle(props, dashes, 2,
                                        crosshair_style.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    // Text formats.
    // GDI used CreateFontW with positive cell-height values (14, 36, 16 px).
    // These point sizes preserve the historical overlay proportions; the
    // render target DPI is handled separately by the target_dpi seam above.
    hr = dwrite_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, kDimTextFormatSizePt, L"",
        text_dim.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    hr = dwrite_factory->CreateTextFormat(
        L"Segoe UI Black", nullptr, DWRITE_FONT_WEIGHT_BLACK, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, kCenterTextFormatSizePt, L"",
        text_center.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    hr = dwrite_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, kHintTextFormatSizePt, L"",
        text_hint.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    {
        std::array<D2D1_COLOR_F, kTextWheelHueStopCount> const stop_colors = {{
            D2D1::ColorF(1.f, 0.f, 0.f), // Red
            D2D1::ColorF(1.f, 1.f, 0.f), // Yellow
            D2D1::ColorF(0.f, 1.f, 0.f), // Green
            D2D1::ColorF(0.f, 1.f, 1.f), // Cyan
            D2D1::ColorF(0.f, 0.f, 1.f), // Blue
            D2D1::ColorF(1.f, 0.f, 1.f), // Magenta
            D2D1::ColorF(1.f, 0.f, 0.f), // Red (wrap)
        }};
        std::array<D2D1_GRADIENT_STOP, kTextWheelHueStopCount> stops{};
        for (size_t index = 0; index < stop_colors.size(); ++index) {
            stops[index].position =
                static_cast<float>(index) / kTextWheelHuePositionDivisor;
            stops[index].color = stop_colors[index];
        }
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stop_coll;
        hr = hwnd_rt->CreateGradientStopCollection(stops.data(),
                                                   static_cast<UINT32>(stops.size()),
                                                   stop_coll.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
        hr = hwnd_rt->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.f, 0.f),
                                                D2D1::Point2F(1.f, 0.f)),
            stop_coll.Get(), text_wheel_hue_brush.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    // Checker brush for highlighter opacity ring segments.
    // 16×16 bitmap (two 8×8 cells): TL=dark, TR=light, BL=light, BR=dark.
    {
        constexpr UINT32 dark_color = 0xFF666666u;  // 0xAARRGGBB: opaque mid-dark grey
        constexpr UINT32 light_color = 0xFF999999u; // 0xAARRGGBB: opaque mid-light grey
        std::array<UINT32, static_cast<size_t>(kCheckerBitmapPx) * kCheckerBitmapPx>
            pixels{};
        for (UINT32 y = 0; y < kCheckerBitmapPx; ++y) {
            for (UINT32 x = 0; x < kCheckerBitmapPx; ++x) {
                bool const left_half = x < kCheckerCellPx;
                bool const top_half = y < kCheckerCellPx;
                pixels[y * kCheckerBitmapPx + x] =
                    ((left_half == top_half) ? dark_color : light_color);
            }
        }
        D2D1_BITMAP_PROPERTIES checker_props{};
        checker_props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        checker_props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
        float const dpi = Target_dpi();
        checker_props.dpiX = dpi;
        checker_props.dpiY = dpi;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> checker_bmp;
        hr = hwnd_rt->CreateBitmap(D2D1::SizeU(kCheckerBitmapPx, kCheckerBitmapPx),
                                   pixels.data(), kCheckerBitmapPx * sizeof(UINT32),
                                   checker_props, checker_bmp.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
        D2D1_BITMAP_BRUSH_PROPERTIES const brush_props =
            D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP);
        hr = hwnd_rt->CreateBitmapBrush(checker_bmp.Get(), brush_props,
                                        checker_brush.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    return true;
}

bool D2DOverlayResources::Create_cache_targets(int width, int height) {
    if (!hwnd_rt) {
        return false;
    }

    UINT32 const uw = static_cast<UINT32>(width);
    UINT32 const uh = static_cast<UINT32>(height);

    // annotations_rt: transparent BGRA premultiplied (annotations drawn over nothing).
    {
        D2D1_PIXEL_FORMAT pf{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
        D2D1_SIZE_F const size_f =
            D2D1::SizeF(static_cast<float>(width), static_cast<float>(height));
        HRESULT const hr = hwnd_rt->CreateCompatibleRenderTarget(
            &size_f, nullptr, &pf, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
            annotations_rt.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
        (void)uw;
        (void)uh;
    }

    // frozen_rt: opaque BGRA (screenshot fills the background completely).
    {
        D2D1_PIXEL_FORMAT pf{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE};
        D2D1_SIZE_F const size_f =
            D2D1::SizeF(static_cast<float>(width), static_cast<float>(height));
        HRESULT const hr = hwnd_rt->CreateCompatibleRenderTarget(
            &size_f, nullptr, &pf, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
            frozen_rt.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    // draft_stroke_rt: transparent BGRA premultiplied (composited live freehand draft).
    {
        D2D1_PIXEL_FORMAT pf{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
        D2D1_SIZE_F const size_f =
            D2D1::SizeF(static_cast<float>(width), static_cast<float>(height));
        HRESULT const hr = hwnd_rt->CreateCompatibleRenderTarget(
            &size_f, nullptr, &pf, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
            draft_stroke_rt.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    // draft_stroke_body_rt: transparent BGRA premultiplied (cached smoothed body).
    {
        D2D1_PIXEL_FORMAT pf{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
        D2D1_SIZE_F const size_f =
            D2D1::SizeF(static_cast<float>(width), static_cast<float>(height));
        HRESULT const hr = hwnd_rt->CreateCompatibleRenderTarget(
            &size_f, nullptr, &pf, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
            draft_stroke_body_rt.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    annotations_valid = false;
    frozen_valid = false;
    draft_stroke_point_count = 0;
    draft_stroke_body_raw_point_count = 0;
    draft_stroke_stable_tail_start_index = 0;
    draft_stroke_style.reset();
    draft_stroke_tip_shape = core::FreehandTipShape::Round;
    draft_stroke_smoothing_mode = core::FreehandSmoothingMode::Off;
    draft_stroke_bitmap_uses_cached_body = false;
    return true;
}

bool D2DOverlayResources::Upload_glyph_bitmaps(
    std::span<OverlayButtonGlyph const *const> glyphs) {
    if (!hwnd_rt) {
        return false;
    }
    size_t const count = std::min(toolbar_glyphs.size(), glyphs.size());
    for (size_t i = 0; i < count; ++i) {
        if (!Upload_glyph(hwnd_rt.Get(), glyphs[i], toolbar_glyphs[i])) {
            return false;
        }
    }
    for (size_t i = count; i < toolbar_glyphs.size(); ++i) {
        toolbar_glyphs[i].Reset();
    }
    return true;
}

ID2D1Bitmap *
D2DOverlayResources::Toolbar_glyph_bitmap(OverlayToolbarGlyphId glyph) const noexcept {
    size_t const index = Overlay_toolbar_glyph_index(glyph);
    if (index >= toolbar_glyphs.size()) {
        return nullptr;
    }
    return toolbar_glyphs[index].Get();
}

void D2DOverlayResources::Invalidate_annotations() noexcept {
    obfuscate_bitmaps.clear();
    text_bitmaps.clear();
    bubble_bitmaps.clear();
    annotations_valid = false;
    frozen_valid = false;
}

void D2DOverlayResources::Invalidate_frozen() noexcept { frozen_valid = false; }

void D2DOverlayResources::Release_device_resources() {
    screenshot.Reset();
    lifted_window_capture.Reset();
    annotations_rt.Reset();
    frozen_rt.Reset();
    draft_stroke_rt.Reset();
    draft_stroke_body_rt.Reset();
    annotations_bitmap.Reset();
    frozen_bitmap.Reset();
    draft_stroke_bitmap.Reset();
    draft_stroke_body_bitmap.Reset();
    draft_stroke_point_count = 0;
    draft_stroke_body_raw_point_count = 0;
    draft_stroke_stable_tail_start_index = 0;
    draft_stroke_style.reset();
    draft_stroke_tip_shape = core::FreehandTipShape::Round;
    draft_stroke_smoothing_mode = core::FreehandSmoothingMode::Off;
    draft_stroke_bitmap_uses_cached_body = false;
    multiply_effect.Reset();
    draft_stroke_composite_effect.Reset();
    solid_brush.Reset();
    round_cap_style.Reset();
    flat_cap_style.Reset();
    dashed_style.Reset();
    crosshair_style.Reset();
    text_dim.Reset();
    text_center.Reset();
    text_hint.Reset();
    text_wheel_hue_brush.Reset();
    checker_brush.Reset();
    for (auto &glyph : toolbar_glyphs) {
        glyph.Reset();
    }
    obfuscate_bitmaps.clear();
    text_bitmaps.clear();
    bubble_bitmaps.clear();
    hwnd_rt.Reset();
    annotations_valid = false;
    frozen_valid = false;
}

void D2DOverlayResources::Release_all() {
    Release_device_resources();
    text_cursor_preview_cache.reset();
    dwrite_factory.Reset();
    factory.Reset();
}

} // namespace greenflame
