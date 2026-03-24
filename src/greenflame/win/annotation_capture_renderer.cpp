#include "greenflame/win/annotation_capture_renderer.h"

#include "greenflame/win/d2d_annotation_draw.h"
#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/pixel_ops.h"

namespace greenflame {

namespace {

constexpr float kDefaultDpi = 96.f;

struct CoInitGuard final {
    bool owned = false;

    explicit CoInitGuard(bool owns) noexcept : owned(owns) {}
    ~CoInitGuard() {
        if (owned) {
            CoUninitialize();
        }
    }

    CoInitGuard(CoInitGuard const &) = delete;
    CoInitGuard &operator=(CoInitGuard const &) = delete;
};

[[nodiscard]] bool
Is_highlighter_annotation(core::Annotation const &annotation) noexcept {
    auto const *freehand =
        std::get_if<core::FreehandStrokeAnnotation>(&annotation.data);
    return freehand != nullptr &&
           freehand->freehand_tip_shape == core::FreehandTipShape::Square;
}

[[nodiscard]] core::RectPx
Annotation_local_bounds(core::Annotation const &annotation,
                        core::RectPx target_bounds) noexcept {
    core::RectPx const bounds = core::Annotation_visual_bounds(annotation);
    return core::RectPx::From_ltrb(
        bounds.left - target_bounds.left, bounds.top - target_bounds.top,
        bounds.right - target_bounds.left, bounds.bottom - target_bounds.top);
}

[[nodiscard]] bool Read_capture_pixels(GdiCaptureResult const &capture,
                                       std::span<uint8_t> pixels) {
    HDC const dc = GetDC(nullptr);
    if (dc == nullptr) {
        return false;
    }

    BITMAPINFOHEADER bmi{};
    Fill_bmi32_top_down(bmi, capture.width, capture.height);
    int const result =
        GetDIBits(dc, capture.bitmap, 0, static_cast<UINT>(capture.height),
                  pixels.data(), reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return result != 0;
}

[[nodiscard]] bool Write_capture_pixels(GdiCaptureResult &capture,
                                        std::span<const uint8_t> pixels) {
    HDC const dc = GetDC(nullptr);
    if (dc == nullptr) {
        return false;
    }

    BITMAPINFOHEADER bmi{};
    Fill_bmi32_top_down(bmi, capture.width, capture.height);
    int const result =
        SetDIBits(dc, capture.bitmap, 0, static_cast<UINT>(capture.height),
                  pixels.data(), reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return result != 0;
}

[[nodiscard]] bool
Create_scratch_render_target(IWICImagingFactory *wic_factory, ID2D1Factory *d2d_factory,
                             int width, int height,
                             Microsoft::WRL::ComPtr<IWICBitmap> &wic_bitmap,
                             Microsoft::WRL::ComPtr<ID2D1RenderTarget> &render_target) {
    if (wic_factory == nullptr || d2d_factory == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    HRESULT hr = wic_factory->CreateBitmap(
        static_cast<UINT>(width), static_cast<UINT>(height),
        GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, wic_bitmap.GetAddressOf());
    if (FAILED(hr) || !wic_bitmap) {
        return false;
    }

    // 96 DPI is correct here: annotation coordinates are in physical pixels (PointPx/
    // RectPx), and this RT targets an in-memory WIC bitmap (not a display surface).
    // At 96 DPI, 1 D2D DIP == 1 pixel, so physical-pixel coordinates map 1:1.
    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        kDefaultDpi, kDefaultDpi);
    hr = d2d_factory->CreateWicBitmapRenderTarget(wic_bitmap.Get(), properties,
                                                  render_target.GetAddressOf());
    return SUCCEEDED(hr) && static_cast<bool>(render_target);
}

} // namespace

bool Render_annotations_into_capture(GdiCaptureResult &capture,
                                     std::span<const core::Annotation> annotations,
                                     core::RectPx target_bounds) {
    if (!capture.Is_valid() || annotations.empty()) {
        return true;
    }

    int const row_bytes = Row_bytes32(capture.width);
    size_t const buffer_size =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(capture.height);
    std::vector<uint8_t> pixels(buffer_size);
    if (!Read_capture_pixels(capture, pixels)) {
        return false;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool owns_com = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        owns_com = false;
    } else if (FAILED(hr)) {
        return false;
    }
    CoInitGuard const coinit_guard(owns_com);

    Microsoft::WRL::ComPtr<ID2D1Factory1> d2d_factory;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           d2d_factory.GetAddressOf());
    if (FAILED(hr) || !d2d_factory) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(wic_factory.GetAddressOf()));
    if (FAILED(hr) || !wic_factory) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> scratch_bitmap;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> scratch_rt;
    if (!Create_scratch_render_target(wic_factory.Get(), d2d_factory.Get(),
                                      capture.width, capture.height, scratch_bitmap,
                                      scratch_rt)) {
        return false;
    }

    D2DAnnotationRenderResources annotation_resources{};
    if (!annotation_resources.Initialize(d2d_factory.Get(), scratch_rt.Get())) {
        return false;
    }
    D2DAnnotationDrawContext const annotation_context =
        annotation_resources.Build_context(d2d_factory.Get());

    std::vector<uint8_t> layer_pixels(buffer_size);
    WICRect const copy_rect = {0, 0, capture.width, capture.height};
    D2D1_MATRIX_3X2_F const annotation_transform =
        D2D1::Matrix3x2F::Translation(-static_cast<float>(target_bounds.left),
                                      -static_cast<float>(target_bounds.top));
    D2D1_MATRIX_3X2_F const identity_transform = D2D1::Matrix3x2F::Identity();

    for (core::Annotation const &annotation : annotations) {
        scratch_rt->BeginDraw();
        scratch_rt->SetTransform(identity_transform);
        scratch_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
        scratch_rt->SetTransform(annotation_transform);
        Draw_d2d_annotation(scratch_rt.Get(), annotation_context, annotation);
        scratch_rt->SetTransform(identity_transform);
        hr = scratch_rt->EndDraw();
        if (FAILED(hr)) {
            return false;
        }

        hr = scratch_bitmap->CopyPixels(&copy_rect, static_cast<UINT>(row_bytes),
                                        static_cast<UINT>(layer_pixels.size()),
                                        layer_pixels.data());
        if (FAILED(hr)) {
            return false;
        }

        core::RectPx const layer_bounds =
            Annotation_local_bounds(annotation, target_bounds);
        if (Is_highlighter_annotation(annotation)) {
            core::Multiply_premultiplied_layer_onto_opaque_pixels(
                pixels, capture.width, capture.height, row_bytes, layer_pixels,
                row_bytes, layer_bounds);
        } else {
            core::Blend_premultiplied_layer_onto_opaque_pixels(
                pixels, capture.width, capture.height, row_bytes, layer_pixels,
                row_bytes, layer_bounds);
        }
    }

    return Write_capture_pixels(capture, pixels);
}

} // namespace greenflame
