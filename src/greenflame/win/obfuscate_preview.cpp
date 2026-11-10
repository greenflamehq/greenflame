#include "greenflame/win/obfuscate_preview.h"

#include "greenflame/win/annotation_capture_renderer.h"
#include "greenflame_core/pixel_ops.h"

namespace greenflame {

std::optional<core::BgraBitmap>
Build_obfuscate_source(GdiCaptureResult const &capture, core::RectPx capture_bounds,
                       core::RectPx bounds,
                       std::span<const core::Annotation> lower_annotations,
                       AnnotationCaptureRenderer &renderer,
                       GdiCaptureResult const *outer, core::RectPx outer_bounds) {
    core::RectPx const normalized_bounds = bounds.Normalized();
    if (normalized_bounds.Is_empty() || !capture.Is_valid()) {
        return std::nullopt;
    }

    // An obfuscate can sit anywhere, like any annotation. The part over the capture
    // pixelates the capture, the part past it what outer shows there, and the rest
    // (off the desktop) stays transparent (alpha 0), which Rasterize_obfuscate keeps
    // transparent and averages nothing from.
    core::RectPx const capture_rect = core::RectPx::From_ltrb(
        capture_bounds.left, capture_bounds.top, capture_bounds.left + capture.width,
        capture_bounds.top + capture.height);
    std::optional<core::RectPx> const inside =
        core::RectPx::Intersect(normalized_bounds, capture_rect);
    if (!inside.has_value() || *inside != normalized_bounds) {
        int const row_bytes = Row_bytes32(normalized_bounds.Width());
        core::BgraBitmap bitmap{
            .width_px = normalized_bounds.Width(),
            .height_px = normalized_bounds.Height(),
            .row_bytes = row_bytes,
            .premultiplied_bgra = std::vector<uint8_t>(
                static_cast<size_t>(row_bytes) *
                    static_cast<size_t>(normalized_bounds.Height()),
                0),
        };
        if (outer != nullptr && outer->Is_valid()) {
            std::optional<core::BgraBitmap> around = Build_obfuscate_source(
                *outer, outer_bounds, normalized_bounds, lower_annotations, renderer);
            if (!around.has_value()) {
                return std::nullopt;
            }
            bitmap = std::move(*around);
        }
        if (!inside.has_value()) {
            return bitmap;
        }
        std::optional<core::BgraBitmap> const part = Build_obfuscate_source(
            capture, capture_bounds, *inside, lower_annotations, renderer);
        if (!part.has_value()) {
            return std::nullopt;
        }
        size_t const part_row = static_cast<size_t>(part->width_px) * 4u;
        for (int32_t row = 0; row < part->height_px; ++row) {
            size_t const from =
                static_cast<size_t>(row) * static_cast<size_t>(part->row_bytes);
            size_t const to =
                static_cast<size_t>(inside->top - normalized_bounds.top + row) *
                    static_cast<size_t>(row_bytes) +
                static_cast<size_t>(inside->left - normalized_bounds.left) * 4u;
            std::copy_n(
                part->premultiplied_bgra.begin() + static_cast<std::ptrdiff_t>(from),
                part_row,
                bitmap.premultiplied_bgra.begin() + static_cast<std::ptrdiff_t>(to));
        }
        return bitmap;
    }

    GdiCaptureResult cropped{};
    int32_t const crop_left = normalized_bounds.left - capture_bounds.left;
    int32_t const crop_top = normalized_bounds.top - capture_bounds.top;
    if (!Crop_capture(capture, crop_left, crop_top, normalized_bounds.Width(),
                      normalized_bounds.Height(), cropped)) {
        return std::nullopt;
    }

    if (!lower_annotations.empty() &&
        !renderer.Render(cropped, lower_annotations, normalized_bounds)) {
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

        std::optional<core::Annotation> rebuilt = core::Rebuild_obfuscate_from_source(
            annotations[index], lower_span, source_provider);
        if (rebuilt.has_value()) {
            patches.push_back({index, std::move(*rebuilt)});
        }
    }
    return patches;
}

} // namespace greenflame
