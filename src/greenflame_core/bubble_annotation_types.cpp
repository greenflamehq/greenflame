#include "greenflame_core/bubble_annotation_types.h"

#include "greenflame_core/color_wheel.h"

namespace greenflame::core {

namespace {

constexpr float kSrgbLinearThreshold = 0.04045f;
constexpr float kSrgbLinearDivisor = 12.92f;
constexpr float kSrgbGamma = 2.4f;
constexpr float kSrgbA = 0.055f;
constexpr float kSrgbB = 1.055f;
constexpr float kLumR = 0.2126f;
constexpr float kLumG = 0.7152f;
constexpr float kLumB = 0.0722f;
constexpr float kLumBlackThreshold = 0.179f;
constexpr float kColorChannelMaxF = 255.0f;
constexpr COLORREF kByteMask = static_cast<COLORREF>(0xFF);

[[nodiscard]] float Srgb_to_linear(float c) noexcept {
    return (c <= kSrgbLinearThreshold) ? c / kSrgbLinearDivisor
                                       : std::pow((c + kSrgbA) / kSrgbB, kSrgbGamma);
}

[[nodiscard]] float Colorref_r_f(COLORREF color) noexcept {
    return static_cast<float>(color & kByteMask) / kColorChannelMaxF;
}

[[nodiscard]] float Colorref_g_f(COLORREF color) noexcept {
    return static_cast<float>((color >> 8u) & kByteMask) / kColorChannelMaxF;
}

[[nodiscard]] float Colorref_b_f(COLORREF color) noexcept {
    return static_cast<float>((color >> 16u) & kByteMask) / kColorChannelMaxF;
}

} // namespace

COLORREF Bubble_text_color(COLORREF bg) noexcept {
    float const lum = kLumR * Srgb_to_linear(Colorref_r_f(bg)) +
                      kLumG * Srgb_to_linear(Colorref_g_f(bg)) +
                      kLumB * Srgb_to_linear(Colorref_b_f(bg));
    return (lum > kLumBlackThreshold) ? Make_colorref(0, 0, 0)
                                      : Make_colorref(255, 255, 255);
}

} // namespace greenflame::core
