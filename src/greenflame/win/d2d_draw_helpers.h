#pragma once

#include "greenflame_core/annotation_types.h"

namespace greenflame {

// Shared D2D coordinate and drawing helpers used by d2d_paint.cpp and
// d2d_annotation_draw.cpp. All functions are inline to allow header-only use.

inline constexpr float kColorChannelMaxF = 255.f;
inline constexpr float kArrowBaseWidth = 10.0f;
inline constexpr float kArrowBaseLength = 18.0f;
inline constexpr float kArrowWidthPerStroke = 2.0f;
inline constexpr float kArrowLengthPerStroke = 4.0f;
inline constexpr float kArrowOverlapPerStroke = 2.0f;
inline constexpr float kHalfPixel = 0.5f;

inline D2D1_POINT_2F Pt(core::PointPx point) {
    return D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));
}

inline D2D1_RECT_F Rect(core::RectPx rect) {
    return D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                       static_cast<float>(rect.right), static_cast<float>(rect.bottom));
}

inline D2D1_COLOR_F Colorref_to_d2d(COLORREF color, float alpha = 1.f) {
    return D2D1::ColorF(static_cast<float>(GetRValue(color)) / kColorChannelMaxF,
                        static_cast<float>(GetGValue(color)) / kColorChannelMaxF,
                        static_cast<float>(GetBValue(color)) / kColorChannelMaxF,
                        alpha);
}

[[nodiscard]] inline float
Alpha_from_opacity_percent(int32_t opacity_percent) noexcept {
    int32_t const clamped =
        std::clamp(opacity_percent, core::StrokeStyle::kMinOpacityPercent,
                   core::StrokeStyle::kMaxOpacityPercent);
    return static_cast<float>(clamped) / 100.f;
}

[[nodiscard]] inline float Cross(D2D1_POINT_2F origin, D2D1_POINT_2F a,
                                 D2D1_POINT_2F b) noexcept {
    float const ax = a.x - origin.x;
    float const ay = a.y - origin.y;
    float const bx = b.x - origin.x;
    float const by = b.y - origin.y;
    return ax * by - ay * bx;
}

struct HullResult final {
    std::array<D2D1_POINT_2F, 8> points = {};
    size_t count = 0;
};

[[nodiscard]] inline HullResult
Build_convex_hull(std::array<D2D1_POINT_2F, 8> points) noexcept {
    std::sort(points.begin(), points.end(), [](D2D1_POINT_2F a, D2D1_POINT_2F b) {
        return std::tie(a.x, a.y) < std::tie(b.x, b.y);
    });
    auto const last =
        std::unique(points.begin(), points.end(), [](D2D1_POINT_2F a, D2D1_POINT_2F b) {
            // Exact float equality is safe: inputs are PointPx integers cast to float,
            // so no precision loss occurs.
            return !(a.x < b.x) && !(b.x < a.x) && !(a.y < b.y) && !(b.y < a.y);
        });
    size_t const count = static_cast<size_t>(std::distance(points.begin(), last));
    std::array<D2D1_POINT_2F, 16> working = {};
    size_t working_size = 0;

    for (size_t i = 0; i < count; ++i) {
        while (working_size >= 2 &&
               Cross(working[working_size - 2], working[working_size - 1], points[i]) <=
                   0.f) {
            --working_size;
        }
        working[working_size++] = points[i];
    }

    size_t const lower_size = working_size;
    if (count > 1) {
        for (size_t i = count - 1; i > 0; --i) {
            while (working_size > lower_size &&
                   Cross(working[working_size - 2], working[working_size - 1],
                         points[i - 1]) <= 0.f) {
                --working_size;
            }
            working[working_size++] = points[i - 1];
        }
    }

    if (working_size > 1) {
        --working_size;
    }

    HullResult result{};
    result.count = working_size;
    for (size_t i = 0; i < working_size; ++i) {
        result.points[i] = working[i];
    }
    return result;
}

} // namespace greenflame
