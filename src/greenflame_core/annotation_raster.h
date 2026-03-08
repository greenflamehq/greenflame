#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {

enum class AnnotationKind : uint8_t {
    Freehand,
};

struct StrokeStyle final {
    static constexpr int32_t kMinWidthPx = 1;
    static constexpr int32_t kDefaultWidthPx = 2;
    static constexpr int32_t kMaxWidthPx = 50;
    static constexpr COLORREF kDefaultColor = static_cast<COLORREF>(0x000000FFu);

    int32_t width_px = kDefaultWidthPx;
    COLORREF color = kDefaultColor;

    constexpr bool operator==(StrokeStyle const &) const noexcept = default;
};

struct AnnotationRaster final {
    RectPx bounds = {};
    std::vector<uint8_t> coverage = {};

    [[nodiscard]] int32_t Width() const noexcept { return bounds.Width(); }
    [[nodiscard]] int32_t Height() const noexcept { return bounds.Height(); }
    [[nodiscard]] bool Is_empty() const noexcept {
        return bounds.Is_empty() || coverage.empty();
    }

    constexpr bool operator==(AnnotationRaster const &) const noexcept = default;
};

struct FreehandStrokeAnnotation final {
    std::vector<PointPx> points = {};
    StrokeStyle style = {};
    AnnotationRaster raster = {};

    constexpr bool
    operator==(FreehandStrokeAnnotation const &) const noexcept = default;
};

struct Annotation final {
    uint64_t id = 0;
    AnnotationKind kind = AnnotationKind::Freehand;
    FreehandStrokeAnnotation freehand = {};

    constexpr bool operator==(Annotation const &) const noexcept = default;
};

struct AnnotationDocument final {
    std::vector<Annotation> annotations = {};
    std::optional<uint64_t> selected_annotation_id = std::nullopt;
    uint64_t next_annotation_id = 1;

    constexpr bool operator==(AnnotationDocument const &) const noexcept = default;
};

[[nodiscard]] AnnotationRaster
Rasterize_freehand_stroke(std::span<const PointPx> points, StrokeStyle style);
[[nodiscard]] RectPx Annotation_bounds(Annotation const &annotation) noexcept;
[[nodiscard]] bool Annotation_hits_point(Annotation const &annotation,
                                         PointPx point) noexcept;
[[nodiscard]] std::optional<size_t>
Index_of_topmost_annotation_at(std::span<const Annotation> annotations,
                               PointPx point) noexcept;
[[nodiscard]] std::optional<size_t>
Index_of_annotation_id(std::span<const Annotation> annotations, uint64_t id) noexcept;
[[nodiscard]] Annotation Translate_annotation(Annotation annotation,
                                              PointPx delta) noexcept;

void Blend_annotation_onto_pixels(std::span<uint8_t> pixels, int width, int height,
                                  int row_bytes, Annotation const &annotation,
                                  RectPx target_bounds) noexcept;
void Blend_annotations_onto_pixels(std::span<uint8_t> pixels, int width, int height,
                                   int row_bytes,
                                   std::span<const Annotation> annotations,
                                   RectPx target_bounds) noexcept;

} // namespace greenflame::core
