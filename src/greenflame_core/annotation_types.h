#pragma once

#include "greenflame_core/bubble_annotation_types.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/selection_handles.h"
#include "greenflame_core/text_annotation_types.h"

namespace greenflame::core {

enum class AnnotationKind : uint8_t {
    Freehand,
    Line,
    Rectangle,
    Ellipse,
    Text,
    Bubble,
};

enum class FreehandTipShape : uint8_t {
    Round,
    Square,
};

struct StrokeStyle final {
    static constexpr int32_t kMinWidthPx = 1;
    static constexpr int32_t kDefaultWidthPx = 2;
    static constexpr int32_t kMaxWidthPx = 50;
    static constexpr int32_t kMinOpacityPercent = 0;
    static constexpr int32_t kDefaultOpacityPercent = 100;
    static constexpr int32_t kMaxOpacityPercent = 100;
    static constexpr COLORREF kDefaultColor = static_cast<COLORREF>(0x00000000u);

    int32_t width_px = kDefaultWidthPx;
    COLORREF color = kDefaultColor;
    // opacity_percent applies to all annotation types, including Line and Rectangle.
    int32_t opacity_percent = kDefaultOpacityPercent;

    constexpr bool operator==(StrokeStyle const &) const noexcept = default;
};

struct FreehandStrokeAnnotation final {
    std::vector<PointPx> points = {};
    StrokeStyle style = {};
    FreehandTipShape freehand_tip_shape = FreehandTipShape::Round;

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

struct EllipseAnnotation final {
    RectPx outer_bounds = {};
    StrokeStyle style = {};
    bool filled = false;

    constexpr bool operator==(EllipseAnnotation const &) const noexcept = default;
};

using AnnotationData =
    std::variant<FreehandStrokeAnnotation, LineAnnotation, RectangleAnnotation,
                 EllipseAnnotation, TextAnnotation, BubbleAnnotation>;

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
    constexpr explicit Overloaded(Ts... ts) : Ts(std::move(ts))... {}
    Overloaded(Overloaded const &) = default;
    Overloaded(Overloaded &&) = default;
    Overloaded &operator=(Overloaded const &) = delete;
    Overloaded &operator=(Overloaded &&) = delete;
};

template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

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
    AnnotationData data{};

    [[nodiscard]] AnnotationKind Kind() const noexcept;

    constexpr bool operator==(Annotation const &) const noexcept = default;
};

struct AnnotationDocument final {
    std::vector<Annotation> annotations = {};
    std::optional<uint64_t> selected_annotation_id = std::nullopt;
    uint64_t next_annotation_id = 1;

    constexpr bool operator==(AnnotationDocument const &) const noexcept = default;
};

[[nodiscard]] bool Annotation_shows_corner_brackets(AnnotationKind kind) noexcept;

} // namespace greenflame::core
