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

struct FreehandStrokeAnnotation final {
    std::vector<PointPx> points = {};
    StrokeStyle style = {};

    constexpr bool
    operator==(FreehandStrokeAnnotation const &) const noexcept = default;
};

struct LineAnnotation final {
    PointPx start = {};
    PointPx end = {};
    StrokeStyle style = {};
    bool arrow_head = false;

    constexpr bool operator==(LineAnnotation const &) const noexcept = default;
};

struct RectangleAnnotation final {
    RectPx outer_bounds = {};
    StrokeStyle style = {};
    bool filled = false;

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

void Blend_annotation_onto_pixels(std::span<uint8_t> pixels, int width, int height,
                                  int row_bytes, Annotation const &annotation,
                                  RectPx target_bounds) noexcept;
void Blend_annotations_onto_pixels(std::span<uint8_t> pixels, int width, int height,
                                   int row_bytes,
                                   std::span<const Annotation> annotations,
                                   RectPx target_bounds) noexcept;

} // namespace greenflame::core
