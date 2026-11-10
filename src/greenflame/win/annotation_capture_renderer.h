#pragma once

#include "greenflame/win/gdi_capture.h"
#include "greenflame_core/annotation_types.h"

namespace greenflame {

// Composites annotations into captures in software (WIC render target). It holds one
// COM reference and one D2D and one WIC factory for its lifetime, so a caller that
// renders often (the obfuscate preview source) does not create them per call. Use it
// on the thread that made it.
class AnnotationCaptureRenderer final {
  public:
    AnnotationCaptureRenderer();
    ~AnnotationCaptureRenderer();
    AnnotationCaptureRenderer(AnnotationCaptureRenderer const &) = delete;
    AnnotationCaptureRenderer &operator=(AnnotationCaptureRenderer const &) = delete;
    AnnotationCaptureRenderer(AnnotationCaptureRenderer &&) = delete;
    AnnotationCaptureRenderer &operator=(AnnotationCaptureRenderer &&) = delete;

    // Draws annotations, in z-order, into capture, whose top-left pixel is
    // target_bounds' top-left in annotation coordinates. Annotations whose visual
    // bounds miss the capture are skipped: they would write nothing.
    [[nodiscard]] bool Render(GdiCaptureResult &capture,
                              std::span<const core::Annotation> annotations,
                              core::RectPx target_bounds);

  private:
    bool com_ok_ = false;
    bool com_owned_ = false;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2d_factory_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_factory_;
};

// One-off render through a fresh AnnotationCaptureRenderer (Save, Copy, CLI).
[[nodiscard]] bool
Render_annotations_into_capture(GdiCaptureResult &capture,
                                std::span<const core::Annotation> annotations,
                                core::RectPx target_bounds);

} // namespace greenflame
