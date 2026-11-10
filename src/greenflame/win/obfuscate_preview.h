#pragma once

// Live obfuscate previews: the pixels an obfuscate pixelates, and the per-frame
// patches the overlay draws while an edit moves annotations under or over them.

#include "greenflame/win/d2d_paint.h"
#include "greenflame/win/gdi_capture.h"
#include "greenflame_core/annotation_controller.h"

namespace greenflame {

class AnnotationCaptureRenderer;

// The capture cut to bounds, with lower_annotations composited over it in z-order:
// what an obfuscate at bounds pixelates. capture_bounds is where the capture's
// top-left pixel sits in overlay client pixels (zero for the desktop screenshot).
[[nodiscard]] std::optional<core::BgraBitmap>
Build_obfuscate_source(GdiCaptureResult const &capture, core::RectPx capture_bounds,
                       core::RectPx bounds,
                       std::span<const core::Annotation> lower_annotations,
                       AnnotationCaptureRenderer &renderer);

// One patch per preview index, in order, built from scratch with no cache: each
// obfuscate's source takes every annotation below it, with the patches already built
// applied. Indices past the end, and obfuscates whose source cannot be built, get no
// patch. The render bench checks core::ObfuscatePatchCache against it.
[[nodiscard]] std::vector<AnnotationPreviewPatch>
Build_obfuscate_preview_patches(std::span<const core::Annotation> annotations,
                                std::span<const size_t> preview_indices,
                                core::IObfuscateSourceProvider &source_provider);

} // namespace greenflame
