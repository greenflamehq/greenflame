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
// Bounds may reach past the capture: the part outside it then comes from outer (at
// outer_bounds; for a window capture, the desktop screenshot the overlay shows around
// the window), and what neither covers is transparent (alpha 0). Only a real failure
// gives nullopt.
[[nodiscard]] std::optional<core::BgraBitmap> Build_obfuscate_source(
    GdiCaptureResult const &capture, core::RectPx capture_bounds, core::RectPx bounds,
    std::span<const core::Annotation> lower_annotations,
    AnnotationCaptureRenderer &renderer, GdiCaptureResult const *outer = nullptr,
    core::RectPx outer_bounds = {});

// One patch per preview index, in order, built from scratch with no cache: each
// obfuscate's source takes every annotation below it, with the patches already built
// applied. Indices past the end, and obfuscates whose source cannot be built, get no
// patch. The render bench checks core::ObfuscatePatchCache against it.
[[nodiscard]] std::vector<AnnotationPreviewPatch>
Build_obfuscate_preview_patches(std::span<const core::Annotation> annotations,
                                std::span<const size_t> preview_indices,
                                core::IObfuscateSourceProvider &source_provider);

} // namespace greenflame
