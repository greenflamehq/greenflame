#pragma once

#include "greenflame_core/rect_px.h"
#include "greenflame_core/selection_handles.h"

namespace greenflame::core {

enum class AnnotationKind : uint8_t {
    Freehand,
    Line,
    Rectangle,
};

struct StrokeStyle final {
    static constexpr int32_t kMinWidthPx = 1;
    static constexpr int32_t kDefaultWidthPx = 2;
    static constexpr int32_t kMaxWidthPx = 50;
    static constexpr COLORREF kDefaultColor = static_cast<COLORREF>(0x00000000u);

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

struct LineAnnotation final {
    PointPx start = {};
    PointPx end = {};
    StrokeStyle style = {};
    bool arrow_head = false;
    AnnotationRaster raster = {};

    constexpr bool operator==(LineAnnotation const &) const noexcept = default;
};

struct RectangleAnnotation final {
    RectPx outer_bounds = {};
    StrokeStyle style = {};
    bool filled = false;
    AnnotationRaster raster = {};

    constexpr bool operator==(RectangleAnnotation const &) const noexcept = default;
};

enum class AnnotationLineEndpoint : uint8_t {
    Start,
    End,
};

inline constexpr int32_t kAnnotationHandleBodySizePx = 10;
inline constexpr int32_t kAnnotationHandleHaloSizePx = 1;
inline constexpr int32_t kAnnotationHandleOuterSizePx =
    kAnnotationHandleBodySizePx + (kAnnotationHandleHaloSizePx * 2);
inline constexpr int32_t kAnnotationHandleHitSizePx = 11;

struct Annotation final {
    uint64_t id = 0;
    AnnotationKind kind = AnnotationKind::Freehand;
    FreehandStrokeAnnotation freehand = {};
    LineAnnotation line = {};
    RectangleAnnotation rectangle = {};

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
[[nodiscard]] AnnotationRaster Rasterize_line_segment(PointPx start, PointPx end,
                                                      StrokeStyle style,
                                                      bool arrow_head = false);
[[nodiscard]] AnnotationRaster
Rasterize_rectangle(RectPx outer_bounds, StrokeStyle style, bool filled) noexcept;
[[nodiscard]] RectPx Annotation_bounds(Annotation const &annotation) noexcept;
[[nodiscard]] bool Annotation_hits_point(Annotation const &annotation,
                                         PointPx point) noexcept;
[[nodiscard]] std::optional<size_t>
Index_of_topmost_annotation_at(std::span<const Annotation> annotations,
                               PointPx point) noexcept;
[[nodiscard]] std::optional<size_t>
Index_of_annotation_id(std::span<const Annotation> annotations, uint64_t id) noexcept;
[[nodiscard]] std::optional<AnnotationLineEndpoint>
Hit_test_line_endpoint_handles(PointPx start, PointPx end, PointPx cursor) noexcept;
[[nodiscard]] RectPx Rectangle_outer_bounds_from_corners(PointPx a, PointPx b) noexcept;
[[nodiscard]] PointPx Rectangle_resize_handle_center(RectPx outer_bounds,
                                                     SelectionHandle handle) noexcept;
[[nodiscard]] std::array<bool, 8>
Visible_rectangle_resize_handles(RectPx outer_bounds) noexcept;
[[nodiscard]] std::optional<SelectionHandle>
Hit_test_rectangle_resize_handles(RectPx outer_bounds, PointPx cursor) noexcept;
[[nodiscard]] RectPx Resize_rectangle_from_handle(RectPx outer_bounds,
                                                  SelectionHandle handle,
                                                  PointPx cursor) noexcept;
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
