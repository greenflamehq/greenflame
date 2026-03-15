#include "greenflame_core/color_wheel.h"

namespace greenflame::core {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegreesPerTurn = 360.0f;
constexpr float kTopGapCenterDegrees = 270.0f;

[[nodiscard]] constexpr float Color_wheel_outer_radius_px() noexcept {
    return static_cast<float>(kColorWheelOuterDiameterPx) / 2.0f;
}

[[nodiscard]] constexpr float Color_wheel_inner_radius_px() noexcept {
    return Color_wheel_outer_radius_px() - kColorWheelWidthPx;
}

[[nodiscard]] constexpr float Color_wheel_mid_radius_px() noexcept {
    return (Color_wheel_outer_radius_px() + Color_wheel_inner_radius_px()) / 2.0f;
}

[[nodiscard]] constexpr float Color_wheel_gap_degrees() noexcept {
    return (kColorWheelSegmentGapPx / Color_wheel_mid_radius_px()) *
           (kDegreesPerTurn / (2.0f * kPi));
}

[[nodiscard]] float Degrees_per_segment(size_t segment_count) noexcept {
    if (segment_count == 0) {
        return 0.0f;
    }
    return kDegreesPerTurn / static_cast<float>(segment_count);
}

[[nodiscard]] constexpr float Normalize_degrees(float angle) noexcept {
    while (angle < 0.0f) {
        angle += kDegreesPerTurn;
    }
    while (angle >= kDegreesPerTurn) {
        angle -= kDegreesPerTurn;
    }
    return angle;
}

[[nodiscard]] constexpr float Smallest_angle_delta_degrees(float from,
                                                           float to) noexcept {
    float delta = Normalize_degrees(to - from);
    if (delta > kDegreesPerTurn / 2.0f) {
        delta -= kDegreesPerTurn;
    }
    return delta;
}

} // namespace

ColorWheelSegmentGeometry
Get_color_wheel_segment_geometry(size_t index, size_t segment_count) noexcept {
    ColorWheelSegmentGeometry geometry{};
    if (segment_count == 0 || index >= segment_count) {
        return geometry;
    }

    float const degrees_per_segment = Degrees_per_segment(segment_count);
    float const first_segment_center_degrees =
        kTopGapCenterDegrees + degrees_per_segment / 2.0f;
    geometry.center_angle_degrees = Normalize_degrees(
        first_segment_center_degrees + static_cast<float>(index) * degrees_per_segment);
    geometry.sweep_angle_degrees = degrees_per_segment - Color_wheel_gap_degrees();
    geometry.start_angle_degrees = Normalize_degrees(
        geometry.center_angle_degrees - geometry.sweep_angle_degrees / 2.0f);
    return geometry;
}

std::optional<size_t> Hit_test_color_wheel_segment(PointPx center, PointPx point,
                                                   size_t segment_count) noexcept {
    if (segment_count == 0) {
        return std::nullopt;
    }

    float const dx = static_cast<float>(point.x - center.x);
    float const dy = static_cast<float>(point.y - center.y);
    float const distance_sq = dx * dx + dy * dy;
    float const inner_radius = Color_wheel_inner_radius_px();
    float const outer_radius = Color_wheel_outer_radius_px();
    if (distance_sq < inner_radius * inner_radius ||
        distance_sq > outer_radius * outer_radius) {
        return std::nullopt;
    }

    float const angle =
        Normalize_degrees(std::atan2(dy, dx) * (kDegreesPerTurn / (2.0f * kPi)));
    for (size_t index = 0; index < segment_count; ++index) {
        ColorWheelSegmentGeometry const geometry =
            Get_color_wheel_segment_geometry(index, segment_count);
        float const half_sweep = geometry.sweep_angle_degrees / 2.0f;
        if (std::fabs(Smallest_angle_delta_degrees(geometry.center_angle_degrees,
                                                   angle)) <= half_sweep) {
            return index;
        }
    }

    return std::nullopt;
}

std::optional<TextWheelHubSide> Hit_test_text_wheel_hub(PointPx center,
                                                        PointPx point) noexcept {
    float const hub_r = Color_wheel_inner_radius_px() - kTextWheelHubRingGapPx;
    float const dx = static_cast<float>(point.x - center.x);
    float const dy = static_cast<float>(point.y - center.y);
    if (dx * dx + dy * dy > hub_r * hub_r) {
        return std::nullopt;
    }
    if (dx >= -kTextWheelHubHalfGapPx && dx <= kTextWheelHubHalfGapPx) {
        return std::nullopt;
    }
    return (dx < -kTextWheelHubHalfGapPx) ? TextWheelHubSide::Color
                                          : TextWheelHubSide::Font;
}

} // namespace greenflame::core
