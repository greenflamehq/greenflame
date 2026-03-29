#include "greenflame/win/annotation_capture_renderer.h"

#include "greenflame/win/d2d_annotation_draw.h"
#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/obfuscate_raster.h"
#include "greenflame_core/pixel_ops.h"

namespace greenflame {

namespace {

constexpr float kCaptureScratchDpi = 96.f;

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

struct DynamicObfuscateLayer final {
    core::RectPx bounds = {};
    core::BgraBitmap bitmap = {};
};

[[nodiscard]] core::RectPx
Local_bounds_for_obfuscate(core::ObfuscateAnnotation const &annotation,
                           core::RectPx target_bounds) noexcept {
    core::RectPx const bounds = annotation.bounds.Normalized();
    return core::RectPx::From_ltrb(
        bounds.left - target_bounds.left, bounds.top - target_bounds.top,
        bounds.right - target_bounds.left, bounds.bottom - target_bounds.top);
}

[[nodiscard]] std::optional<core::BgraBitmap>
Extract_bitmap_from_pixels(std::span<const uint8_t> pixels, int width, int height,
                           int row_bytes, core::RectPx bounds) {
    std::optional<core::RectPx> const clipped_bounds =
        core::RectPx::Clip(bounds, core::RectPx::From_ltrb(0, 0, width, height));
    if (!clipped_bounds.has_value()) {
        return std::nullopt;
    }

    int32_t const clipped_width = clipped_bounds->Width();
    int32_t const clipped_height = clipped_bounds->Height();
    int32_t const clipped_row_bytes = clipped_width * 4;
    core::BgraBitmap bitmap{
        .width_px = clipped_width,
        .height_px = clipped_height,
        .row_bytes = clipped_row_bytes,
        .premultiplied_bgra =
            std::vector<uint8_t>(static_cast<size_t>(clipped_row_bytes) *
                                 static_cast<size_t>(clipped_height)),
    };
    std::span<uint8_t> const destination_pixels(bitmap.premultiplied_bgra);
    for (int32_t row = 0; row < clipped_height; ++row) {
        size_t const source_offset = (static_cast<size_t>(clipped_bounds->top + row) *
                                      static_cast<size_t>(row_bytes)) +
                                     (static_cast<size_t>(clipped_bounds->left) * 4u);
        size_t const destination_offset =
            static_cast<size_t>(row) * static_cast<size_t>(clipped_row_bytes);
        std::span<const uint8_t> const source_row =
            pixels.subspan(source_offset, static_cast<size_t>(clipped_row_bytes));
        std::span<uint8_t> const destination_row = destination_pixels.subspan(
            destination_offset, static_cast<size_t>(clipped_row_bytes));
        std::ranges::copy(source_row, destination_row.begin());
    }
    core::Force_alpha_opaque(destination_pixels);
    return bitmap;
}

[[nodiscard]] std::optional<DynamicObfuscateLayer> Build_dynamic_obfuscate_layer(
    std::span<const uint8_t> pixels, int width, int height, int row_bytes,
    core::ObfuscateAnnotation const &annotation, core::RectPx target_bounds) {
    core::RectPx const local_bounds =
        Local_bounds_for_obfuscate(annotation, target_bounds);
    std::optional<core::RectPx> const clipped_bounds =
        core::RectPx::Clip(local_bounds, core::RectPx::From_ltrb(0, 0, width, height));
    if (!clipped_bounds.has_value()) {
        return std::nullopt;
    }

    std::optional<core::BgraBitmap> const source =
        Extract_bitmap_from_pixels(pixels, width, height, row_bytes, *clipped_bounds);
    if (!source.has_value() || !source->Is_valid()) {
        return std::nullopt;
    }

    core::BgraBitmap const raster =
        core::Rasterize_obfuscate(*source, annotation.block_size);
    if (!raster.Is_valid()) {
        return std::nullopt;
    }

    return DynamicObfuscateLayer{*clipped_bounds, raster};
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

    // This WIC render target is intentionally 96 DPI because capture compositing is
    // still pixel-space output. At 96 DPI, 1 D2D DIP == 1 pixel, so the scratch
    // surface maps capture pixels 1:1.
    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        kCaptureScratchDpi, kCaptureScratchDpi);
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
        if (core::ObfuscateAnnotation const *const obfuscate =
                std::get_if<core::ObfuscateAnnotation>(&annotation.data);
            obfuscate != nullptr && obfuscate->premultiplied_bgra.empty()) {
            std::optional<DynamicObfuscateLayer> const layer =
                Build_dynamic_obfuscate_layer(pixels, capture.width, capture.height,
                                              row_bytes, *obfuscate, target_bounds);
            if (!layer.has_value()) {
                continue;
            }

            core::Blend_premultiplied_bitmap_onto_opaque_pixels(
                pixels, capture.width, capture.height, row_bytes,
                layer->bitmap.premultiplied_bgra, layer->bitmap.width_px,
                layer->bitmap.height_px, layer->bitmap.row_bytes, layer->bounds);
            continue;
        }

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
