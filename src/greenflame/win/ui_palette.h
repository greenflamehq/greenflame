#pragma once

namespace greenflame {

[[nodiscard]] constexpr D2D1_COLOR_F
Make_d2d_color(uint8_t red, uint8_t green, uint8_t blue, float alpha = 1.f) noexcept {
    constexpr float d2d_color_scale = 1.f / 255.f;
    return {static_cast<float>(red) * d2d_color_scale,
            static_cast<float>(green) * d2d_color_scale,
            static_cast<float>(blue) * d2d_color_scale, alpha};
}

// Shared Direct2D overlay palette.
inline constexpr D2D1_COLOR_F kBorderColor =
    Make_d2d_color(135, 223, 0); // Selection border, crosshair, help accent
inline constexpr D2D1_COLOR_F kCoordTooltipBg = Make_d2d_color(217, 240, 227);
inline constexpr D2D1_COLOR_F kCoordTooltipText =
    Make_d2d_color(26, 121, 6); // Tooltip text + border
inline constexpr float kCoordTooltipAlpha = 1.f;
inline constexpr D2D1_COLOR_F kOverlayButtonFillColor = kCoordTooltipBg;
inline constexpr D2D1_COLOR_F kOverlayButtonOutlineColor = kCoordTooltipText;
inline constexpr D2D1_COLOR_F kMagnifierCheckerDark = Make_d2d_color(168, 168, 168);
inline constexpr D2D1_COLOR_F kMagnifierCheckerLight = Make_d2d_color(224, 224, 224);

// GDI/GDI+ toast palette.
inline constexpr COLORREF kToastBackground = RGB(250, 250, 250);
inline constexpr COLORREF kToastTitleText = RGB(17, 17, 17);
inline constexpr COLORREF kToastBodyText = RGB(32, 32, 32);
inline constexpr COLORREF kToastBorder = RGB(213, 213, 213);
inline constexpr COLORREF kToastAccentInfo = RGB(46, 139, 87);
inline constexpr COLORREF kToastAccentWarning = RGB(240, 173, 78);
inline constexpr COLORREF kToastAccentError = RGB(217, 83, 79);
inline constexpr COLORREF kToastIconGlyphLight = RGB(255, 255, 255);
inline constexpr COLORREF kToastIconGlyphWarning = RGB(64, 48, 0);
inline constexpr COLORREF kToastLinkText = RGB(0, 102, 204);

} // namespace greenflame
