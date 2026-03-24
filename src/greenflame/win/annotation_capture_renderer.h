#pragma once

#include "greenflame/win/gdi_capture.h"
#include "greenflame_core/annotation_types.h"

namespace greenflame {

[[nodiscard]] bool
Render_annotations_into_capture(GdiCaptureResult &capture,
                                std::span<const core::Annotation> annotations,
                                core::RectPx target_bounds);

} // namespace greenflame
