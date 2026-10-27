#pragma once

// Live obfuscate previews: the pixels an obfuscate pixelates, and the per-frame
// patches the overlay draws while an edit moves annotations under or over them.

#include "greenflame/win/d2d_paint.h"
#include "greenflame/win/gdi_capture.h"
#include "greenflame_core/annotation_controller.h"

namespace greenflame {

// The capture cut to bounds, with lower_annotations composited over it in z-order:
// what an obfuscate at bounds pixelates. capture_bounds is where the capture's
// top-left pixel sits in overlay client pixels (zero for the desktop screenshot).
[[nodiscard]] std::optional<core::BgraBitmap>
Build_obfuscate_source(GdiCaptureResult const &capture, core::RectPx capture_bounds,
                       core::RectPx bounds,
                       std::span<const core::Annotation> lower_annotations);

// The obfuscate annotation with its raster rebuilt from its current source, or nullopt
// when annotation is not an obfuscate or its source cannot be built.
[[nodiscard]] std::optional<core::Annotation>
Build_preview_obfuscate_annotation(core::Annotation annotation,
                                   std::span<const core::Annotation> lower_annotations,
                                   core::IObfuscateSourceProvider &source_provider);

// One patch per preview index, in order. Each obfuscate's source takes the patches
// already built below it, so stacked obfuscates see each other's new rasters.
// Indices past the end, and obfuscates whose source cannot be built, get no patch.
[[nodiscard]] std::vector<AnnotationPreviewPatch>
Build_obfuscate_preview_patches(std::span<const core::Annotation> annotations,
                                std::span<const size_t> preview_indices,
                                core::IObfuscateSourceProvider &source_provider);

} // namespace greenflame
