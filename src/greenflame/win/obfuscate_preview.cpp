#include "greenflame/win/obfuscate_preview.h"

#include "greenflame/win/annotation_capture_renderer.h"
#include "greenflame_core/obfuscate_raster.h"
#include "greenflame_core/pixel_ops.h"

namespace greenflame {

namespace {

[[nodiscard]] bool Read_capture_pixels(GdiCaptureResult const &capture,
                                       std::span<uint8_t> pixels) {
    if (!capture.Is_valid()) {
        return false;
    }

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

} // namespace

std::optional<core::BgraBitmap>
Build_obfuscate_source(GdiCaptureResult const &capture, core::RectPx capture_bounds,
                       core::RectPx bounds,
                       std::span<const core::Annotation> lower_annotations) {
    core::RectPx const normalized_bounds = bounds.Normalized();
    if (normalized_bounds.Is_empty() || !capture.Is_valid()) {
        return std::nullopt;
    }

    GdiCaptureResult cropped{};
    int32_t const crop_left = normalized_bounds.left - capture_bounds.left;
    int32_t const crop_top = normalized_bounds.top - capture_bounds.top;
    if (!Crop_capture(capture, crop_left, crop_top, normalized_bounds.Width(),
                      normalized_bounds.Height(), cropped)) {
        return std::nullopt;
    }

    if (!lower_annotations.empty() &&
        !Render_annotations_into_capture(cropped, lower_annotations,
                                         normalized_bounds)) {
        cropped.Free();
        return std::nullopt;
    }

    int const row_bytes = Row_bytes32(cropped.width);
    size_t const buffer_size =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(cropped.height);
    core::BgraBitmap bitmap{
        .width_px = cropped.width,
        .height_px = cropped.height,
        .row_bytes = row_bytes,
        .premultiplied_bgra = std::vector<uint8_t>(buffer_size),
    };
    bool const read_ok = Read_capture_pixels(cropped, bitmap.premultiplied_bgra);
    cropped.Free();
    if (!read_ok) {
        return std::nullopt;
    }

    core::Force_alpha_opaque(bitmap.premultiplied_bgra);
    return bitmap;
}

std::optional<core::Annotation>
Build_preview_obfuscate_annotation(core::Annotation annotation,
                                   std::span<const core::Annotation> lower_annotations,
                                   core::IObfuscateSourceProvider &source_provider) {
    core::ObfuscateAnnotation *const obfuscate =
        std::get_if<core::ObfuscateAnnotation>(&annotation.data);
    if (obfuscate == nullptr) {
        return std::nullopt;
    }

    core::RectPx const normalized_bounds = obfuscate->bounds.Normalized();
    if (normalized_bounds.Is_empty()) {
        return std::nullopt;
    }

    std::optional<core::BgraBitmap> const source =
        source_provider.Build_composited_source(normalized_bounds, lower_annotations);
    if (!source.has_value() || !source->Is_valid()) {
        return std::nullopt;
    }

    core::BgraBitmap const raster =
        core::Rasterize_obfuscate(*source, obfuscate->block_size);
    if (!raster.Is_valid()) {
        return std::nullopt;
    }

    obfuscate->bounds = normalized_bounds;
    obfuscate->block_size = core::Clamp_obfuscate_block_size(obfuscate->block_size);
    obfuscate->bitmap_width_px = raster.width_px;
    obfuscate->bitmap_height_px = raster.height_px;
    obfuscate->bitmap_row_bytes = raster.row_bytes;
    obfuscate->premultiplied_bgra = raster.premultiplied_bgra;
    return annotation;
}

std::vector<AnnotationPreviewPatch>
Build_obfuscate_preview_patches(std::span<const core::Annotation> annotations,
                                std::span<const size_t> preview_indices,
                                core::IObfuscateSourceProvider &source_provider) {
    std::vector<AnnotationPreviewPatch> patches;
    patches.reserve(preview_indices.size());
    for (size_t const index : preview_indices) {
        if (index >= annotations.size()) {
            continue;
        }

        // Lower annotations with the patches already built applied, for correct
        // stacking when several obfuscates overlap.
        std::span<const core::Annotation> lower_span = annotations.first(index);
        std::vector<core::Annotation> lower_scratch;
        for (AnnotationPreviewPatch const &patch : patches) {
            if (patch.index < index) {
                if (lower_scratch.empty()) {
                    lower_scratch.assign(lower_span.begin(), lower_span.end());
                }
                lower_scratch[patch.index] = patch.annotation;
            }
        }
        if (!lower_scratch.empty()) {
            lower_span = lower_scratch;
        }

        std::optional<core::Annotation> rebuilt = Build_preview_obfuscate_annotation(
            annotations[index], lower_span, source_provider);
        if (rebuilt.has_value()) {
            patches.push_back({index, std::move(*rebuilt)});
        }
    }
    return patches;
}

} // namespace greenflame
