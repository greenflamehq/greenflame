// Debug overlay button: numbered circle for testing toolbar placement.

#include "win/overlay_button.h"

namespace greenflame {

namespace {

constexpr BYTE kOpaqueAlpha = 255;
constexpr Gdiplus::REAL kPenWidth = 1.5f;
constexpr Gdiplus::REAL kRingInset = 3.5f;
constexpr Gdiplus::REAL kDebugFontSize = 12.0f;

// GDI+ startup — idempotent, called at most once per process.
[[nodiscard]] bool Ensure_gdiplus() noexcept {
    static ULONG_PTR s_token = 0;
    static bool s_ok = false;
    if (!s_ok) {
        Gdiplus::GdiplusStartupInput input;
        s_ok = Gdiplus::GdiplusStartup(&s_token, &input, nullptr) == Gdiplus::Ok;
    }
    return s_ok;
}

[[nodiscard]] Gdiplus::Color To_gdip(COLORREF c, BYTE alpha = kOpaqueAlpha) noexcept {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

} // namespace

DebugNumberButton::DebugNumberButton(core::PointPx position, int diameter,
                                     int zero_based_index)
    : position_(position), diameter_(diameter), index_(zero_based_index) {}

core::RectPx DebugNumberButton::Bounds() const {
    return core::RectPx::From_ltrb(position_.x, position_.y, position_.x + diameter_,
                                   position_.y + diameter_);
}

bool DebugNumberButton::Hit_test(core::PointPx pt) const {
    int const cx = position_.x + diameter_ / 2;
    int const cy = position_.y + diameter_ / 2;
    int const r = diameter_ / 2;
    int const dx = pt.x - cx;
    int const dy = pt.y - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

void DebugNumberButton::Set_position(core::PointPx top_left) { position_ = top_left; }

void DebugNumberButton::Draw(HDC dc, ButtonDrawContext const &ctx) const {
    if (!dc || !Ensure_gdiplus()) {
        return;
    }

    // Render into an off-screen ARGB bitmap so GDI+ composites against a known
    // transparent background.  AlphaBlend then composites the result onto dc
    // using the destination's actual RGB values regardless of its alpha byte
    // (which GDI/GetDIBits/SetDIBits leaves as 0), giving correct source-over
    // blending against the captured screen content.
    Gdiplus::REAL const df = static_cast<Gdiplus::REAL>(diameter_);
    Gdiplus::Bitmap bmp(diameter_, diameter_, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        constexpr Gdiplus::REAL k_half_pen = kPenWidth / 2.0f;

        // Filled circle.
        {
            Gdiplus::SolidBrush fill(To_gdip(ctx.fill_color));
            g.FillEllipse(&fill, 0.0f, 0.0f, df, df);
        }

        // Outline ring.
        {
            Gdiplus::Pen outline(To_gdip(ctx.outline_color), kPenWidth);
            g.DrawEllipse(&outline, k_half_pen, k_half_pen, df - kPenWidth,
                          df - kPenWidth);
        }

        // Hover: white inner ring.
        if (hovered_) {
            constexpr Gdiplus::REAL k_ring_double = 2.0f;
            Gdiplus::Pen ring(
                Gdiplus::Color(kOpaqueAlpha, kOpaqueAlpha, kOpaqueAlpha,
                              kOpaqueAlpha),
                kPenWidth);
            g.DrawEllipse(&ring, kRingInset, kRingInset,
                          df - k_ring_double * kRingInset,
                          df - k_ring_double * kRingInset);
        }

        // Label (1-based number) with antialiased GDI+ text (no ClearType fringing).
        {
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
            Gdiplus::Font font(L"Segoe UI", kDebugFontSize, Gdiplus::FontStyleBold,
                               Gdiplus::UnitPixel);
            Gdiplus::SolidBrush text_brush(To_gdip(ctx.outline_color));
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF const text_rect(0.0f, 0.0f, df, df);
            std::wstring const label = std::to_wstring(index_ + 1);
            g.DrawString(label.c_str(), static_cast<INT>(label.size()), &font,
                         text_rect, &fmt, &text_brush);
        }
    }

    // GetHBITMAP with a transparent background premultiplies each pixel's RGB
    // by its alpha, producing the PARGB format that AlphaBlend expects.
    HBITMAP hbm = nullptr;
    if (bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbm) == Gdiplus::Ok && hbm) {
        HDC mem_dc = CreateCompatibleDC(dc);
        if (mem_dc) {
            HGDIOBJ const old = SelectObject(mem_dc, hbm);
            BLENDFUNCTION const bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            AlphaBlend(dc, position_.x, position_.y, diameter_, diameter_, mem_dc, 0, 0,
                       diameter_, diameter_, bf);
            SelectObject(mem_dc, old);
            DeleteDC(mem_dc);
        }
        DeleteObject(hbm);
    }
}

} // namespace greenflame
