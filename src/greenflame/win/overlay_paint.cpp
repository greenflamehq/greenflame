// Overlay painting: capture blit, selection dim/border, dimension labels,
// crosshair, round magnifier and coord tooltip. Implementation and internal
// helpers.

#include "overlay_paint.h"

#include "gdi_capture.h"
#include "greenflame_core/pixel_ops.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/selection_handles.h"
#include "overlay_button.h"
#include "ui_palette.h"

namespace {

constexpr int kMagnifierSize = 256;
constexpr int kMagnifierZoom = 8; // source size = kMagnifierSize / kMagnifierZoom
constexpr int kMagnifierSource = kMagnifierSize / kMagnifierZoom; // 32
constexpr int kMagnifierPadding = 8;
static_assert((kMagnifierZoom % 2) == 0,
              "Magnifier zoom must be even for centered pixel alignment.");
// Magnifier crosshair and contour: 66% opaque.
constexpr unsigned char kMagnifierCrosshairAlpha = 168; // 255 * 66 / 100
// Greenshot-style: thick cross with white contour, center gap, margin from edge.
constexpr int kMagnifierCrosshairThickness = 8;
constexpr int kMagnifierCrosshairGap = 20;   // center to inner arm end
constexpr int kMagnifierCrosshairMargin = 8; // circle edge to outer arm end
static_assert(kMagnifierCrosshairThickness == kMagnifierZoom,
              "Magnifier crosshair thickness must match one magnified pixel.");
constexpr Gdiplus::REAL kMagnifierBorderWidth = 1.5f;
constexpr int kMagnifierCheckerTile = 16;
constexpr COLORREF kMagnifierCheckerLight = RGB(224, 224, 224);
constexpr COLORREF kMagnifierCheckerDark = RGB(168, 168, 168);
constexpr int kColorChannelMax = 255;
constexpr float kColorChannelMaxF = static_cast<float>(kColorChannelMax);

constexpr int kSelDashPx = 4;
constexpr int kSelGapPx = 3;
constexpr int kCornerHighlightOutsetPx = 1;
constexpr int kToolbarTooltipOffsetPx = 6;
constexpr int kToolbarTooltipMarginPx = 4;

void Measure_text_size(HDC dc, HFONT font, std::wstring const &text, SIZE &text_size);

// Draws a 1-pixel dashed rectangle border using explicit MoveToEx/LineTo
// segments so that dash and gap lengths are exact physical pixels regardless
// of DPI scaling in the DC.
void Draw_dashed_rect_border(HDC dc, HPEN pen, int left, int top, int right,
                             int bottom) {
    HGDIOBJ old = SelectObject(dc, pen);
    constexpr int dash = kSelDashPx;
    constexpr int gap = kSelGapPx;
    constexpr int period = dash + gap;
    // Top and bottom rows
    for (int x = left; x < right; x += period) {
        int x2 = std::min(x + dash, right);
        MoveToEx(dc, x, top, nullptr);
        LineTo(dc, x2, top);
        MoveToEx(dc, x, bottom - 1, nullptr);
        LineTo(dc, x2, bottom - 1);
    }
    // Left and right columns (full height, independent segments from top corner)
    for (int y = top; y < bottom; y += period) {
        int y2 = std::min(y + dash, bottom);
        MoveToEx(dc, left, y, nullptr);
        LineTo(dc, left, y2);
        MoveToEx(dc, right - 1, y, nullptr);
        LineTo(dc, right - 1, y2);
    }
    SelectObject(dc, old);
}

void Blend_magnifier_crosshair_onto_pixels(std::span<uint8_t> pixels, int width,
                                           int height, int row_bytes, int mag_left,
                                           int mag_top, int crosshair_left,
                                           int crosshair_top) noexcept {
    int const size = kMagnifierSize;
    int const circle_center = size / 2;
    int const radius_sq = circle_center * circle_center;
    int const half = kMagnifierCrosshairThickness / 2;
    int const gap = kMagnifierCrosshairGap;
    int const margin = kMagnifierCrosshairMargin;
    int const arm_left =
        std::clamp(crosshair_left, 0, size - kMagnifierCrosshairThickness);
    int const arm_top =
        std::clamp(crosshair_top, 0, size - kMagnifierCrosshairThickness);
    int const center_x = arm_left + half;
    int const center_y = arm_top + half;
    float const a = static_cast<float>(kMagnifierCrosshairAlpha) / 255.f;
    float const ia = 1.f - a;

    // Four arm rectangles in magnifier-local coords [x0,x1) x [y0,y1).
    struct Arm {
        int x0, y0, x1, y1;
    };
    Arm const arms[4] = {
        {arm_left, margin, arm_left + kMagnifierCrosshairThickness,
         center_y - gap}, // top
        {arm_left, center_y + gap, arm_left + kMagnifierCrosshairThickness,
         size - margin}, // bottom
        {margin, arm_top, center_x - gap,
         arm_top + kMagnifierCrosshairThickness}, // left
        {center_x + gap, arm_top, size - margin,
         arm_top + kMagnifierCrosshairThickness}, // right
    };

    int const sy0 = std::max(0, mag_top);
    int const sy1 = std::min(height, mag_top + size);
    int const sx0 = std::max(0, mag_left);
    int const sx1 = std::min(width, mag_left + size);

    for (int y = sy0; y < sy1; ++y) {
        int const iy = y - mag_top;
        int const dy_sq = (iy - circle_center) * (iy - circle_center);
        size_t const row_offset =
            static_cast<size_t>(y) * static_cast<size_t>(row_bytes);
        for (int x = sx0; x < sx1; ++x) {
            int const ix = x - mag_left;
            if ((ix - circle_center) * (ix - circle_center) + dy_sq > radius_sq) {
                continue;
            }

            // Classify: inside an arm (black) or on its 1px contour (white)?
            bool on_arm = false;
            bool on_contour = false;
            for (auto const &r : arms) {
                if (ix >= r.x0 && ix < r.x1 && iy >= r.y0 && iy < r.y1) {
                    on_arm = true;
                    break;
                }
                if (ix >= r.x0 - 1 && ix < r.x1 + 1 && iy >= r.y0 - 1 &&
                    iy < r.y1 + 1) {
                    on_contour = true;
                }
            }
            if (on_arm) on_contour = false;
            if (!on_arm && !on_contour) continue;

            size_t const off = static_cast<size_t>(x) * 4;
            if (off + 2 >= static_cast<size_t>(row_bytes)) continue;
            size_t const base = row_offset + off;
            float const target = on_contour ? kColorChannelMaxF : 0.f;
            int const blend_b = static_cast<int>(ia * pixels[base] + a * target);
            int const blend_g = static_cast<int>(ia * pixels[base + 1] + a * target);
            int const blend_r = static_cast<int>(ia * pixels[base + 2] + a * target);
            pixels[base] = static_cast<uint8_t>(
                blend_b > kColorChannelMax ? kColorChannelMax : blend_b);
            pixels[base + 1] = static_cast<uint8_t>(
                blend_g > kColorChannelMax ? kColorChannelMax : blend_g);
            pixels[base + 2] = static_cast<uint8_t>(
                blend_r > kColorChannelMax ? kColorChannelMax : blend_r);
        }
    }
}

void Fill_magnifier_checkerboard(HDC dc, int left, int top, int src_x, int src_y,
                                 int dst_offset_x, int dst_offset_y) noexcept {
    HBRUSH const dc_brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    if (!dc_brush) {
        RECT fallback = {left, top, left + kMagnifierSize, top + kMagnifierSize};
        FillRect(dc, &fallback, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return;
    }

    // Anchor the checker tiles to source-pixel coordinates so that any given
    // tile always maps to the same pair of source pixels, regardless of where
    // the magnifier is positioned. Each tile is 2 source pixels wide/tall =
    // kMagnifierCheckerTile magnifier pixels at kMagnifierZoom zoom.
    //
    // Round src down to the nearest even source pixel (tile boundary). This
    // boundary maps to magnifier offset 0 (even src) or -kMagnifierZoom (odd
    // src, so the first partial tile is half-width).
    int const tile_sx0 = src_x & ~1; // even source pixel at or before src_x
    int const tile_sy0 = src_y & ~1;
    int const mx0 =
        (tile_sx0 - src_x) * kMagnifierZoom + dst_offset_x; // shifted sample origin
    int const my0 = (tile_sy0 - src_y) * kMagnifierZoom + dst_offset_y;
    int const base_tx = tile_sx0 / 2; // tile index of the first tile
    int const base_ty = tile_sy0 / 2;

    for (int ty = 0;; ++ty) {
        int const my = my0 + ty * kMagnifierCheckerTile;
        if (my >= kMagnifierSize) break;
        int const cell_top = top + std::max(0, my);
        int const cell_bottom =
            top + std::min(kMagnifierSize, my + kMagnifierCheckerTile);
        if (cell_bottom <= cell_top) continue;
        for (int tx = 0;; ++tx) {
            int const mx = mx0 + tx * kMagnifierCheckerTile;
            if (mx >= kMagnifierSize) break;
            int const cell_left = left + std::max(0, mx);
            int const cell_right =
                left + std::min(kMagnifierSize, mx + kMagnifierCheckerTile);
            if (cell_right <= cell_left) continue;
            bool const dark = ((base_tx + tx + base_ty + ty) & 1) != 0;
            SetDCBrushColor(dc, dark ? kMagnifierCheckerDark : kMagnifierCheckerLight);
            RECT cell = {cell_left, cell_top, cell_right, cell_bottom};
            FillRect(dc, &cell, dc_brush);
        }
    }
}

[[nodiscard]] COLORREF Get_pen_color_or_fallback(HPEN pen, COLORREF fallback) noexcept {
    if (!pen) {
        return fallback;
    }
    LOGPEN log_pen = {};
    if (GetObjectW(pen, static_cast<int>(sizeof(log_pen)), &log_pen) ==
        static_cast<int>(sizeof(log_pen))) {
        return log_pen.lopnColor;
    }
    return fallback;
}

[[nodiscard]] bool Ensure_gdiplus() noexcept {
    static ULONG_PTR token = 0;
    static bool ok = false;
    if (!ok) {
        Gdiplus::GdiplusStartupInput input;
        ok = Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
    }
    return ok;
}

void Draw_antialiased_magnifier_border(HDC dc, int left, int top,
                                       COLORREF color) noexcept {
    if (!Ensure_gdiplus()) {
        return;
    }
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::Color const gdip_color(255, GetRValue(color), GetGValue(color),
                                    GetBValue(color));
    Gdiplus::Pen pen(gdip_color, kMagnifierBorderWidth);
    Gdiplus::REAL const inset = kMagnifierBorderWidth / 2.0f;
    Gdiplus::REAL const x = static_cast<Gdiplus::REAL>(left) + inset;
    Gdiplus::REAL const y = static_cast<Gdiplus::REAL>(top) + inset;
    Gdiplus::REAL const d =
        static_cast<Gdiplus::REAL>(kMagnifierSize) - kMagnifierBorderWidth;
    (void)graphics.DrawEllipse(&pen, x, y, d, d);
}

void Draw_capture_to_buffer(HDC buf_dc, HDC hdc, int w, int h,
                            greenflame::GdiCaptureResult const *capture) {
    if (!capture || !capture->Is_valid()) return;
    HDC src_dc = CreateCompatibleDC(hdc);
    if (src_dc) {
        HGDIOBJ old_src = SelectObject(src_dc, capture->bitmap);
        BitBlt(buf_dc, 0, 0, w, h, src_dc, 0, 0, SRCCOPY);
        SelectObject(src_dc, old_src);
        DeleteDC(src_dc);
    }
}

void Draw_border_highlight(HDC dc, HPEN pen, greenflame::core::RectPx const &sel,
                           greenflame::core::SelectionHandle highlight,
                           int max_corner_size_px) {
    if (sel.Is_empty()) return;
    greenflame::core::RectPx const r = sel.Normalized();
    int const corner_w = std::min(max_corner_size_px, r.Width() / 2);
    int const corner_h = std::min(max_corner_size_px, r.Height() / 2);
    HGDIOBJ old_pen = SelectObject(dc, pen);

    switch (highlight) {
    case greenflame::core::SelectionHandle::Top:
        for (int off = -kCornerHighlightOutsetPx; off <= kCornerHighlightOutsetPx;
             ++off) {
            MoveToEx(dc, r.left + corner_w, r.top + off, nullptr);
            LineTo(dc, r.right - corner_w, r.top + off);
        }
        break;
    case greenflame::core::SelectionHandle::Bottom:
        for (int off = -kCornerHighlightOutsetPx; off <= kCornerHighlightOutsetPx;
             ++off) {
            MoveToEx(dc, r.left + corner_w, r.bottom - 1 + off, nullptr);
            LineTo(dc, r.right - corner_w, r.bottom - 1 + off);
        }
        break;
    case greenflame::core::SelectionHandle::Left:
        for (int off = -kCornerHighlightOutsetPx; off <= kCornerHighlightOutsetPx;
             ++off) {
            MoveToEx(dc, r.left + off, r.top + corner_h, nullptr);
            LineTo(dc, r.left + off, r.bottom - corner_h);
        }
        break;
    case greenflame::core::SelectionHandle::Right:
        for (int off = -kCornerHighlightOutsetPx; off <= kCornerHighlightOutsetPx;
             ++off) {
            MoveToEx(dc, r.right - 1 + off, r.top + corner_h, nullptr);
            LineTo(dc, r.right - 1 + off, r.bottom - corner_h);
        }
        break;
    case greenflame::core::SelectionHandle::TopLeft:
        for (int off = -kCornerHighlightOutsetPx; off <= kCornerHighlightOutsetPx;
             ++off) {
            MoveToEx(dc, r.left, r.top + off, nullptr);
            LineTo(dc, r.left + corner_w, r.top + off);
            MoveToEx(dc, r.left + off, r.top - kCornerHighlightOutsetPx, nullptr);
            LineTo(dc, r.left + off, r.top + corner_h);
        }
        break;
    case greenflame::core::SelectionHandle::TopRight:
        for (int off = -kCornerHighlightOutsetPx; off <= kCornerHighlightOutsetPx;
             ++off) {
            MoveToEx(dc, r.right - corner_w, r.top + off, nullptr);
            LineTo(dc, r.right, r.top + off);
            MoveToEx(dc, r.right - 1 + off, r.top - kCornerHighlightOutsetPx, nullptr);
            LineTo(dc, r.right - 1 + off, r.top + corner_h);
        }
        break;
    case greenflame::core::SelectionHandle::BottomLeft:
        for (int off = -kCornerHighlightOutsetPx; off <= kCornerHighlightOutsetPx;
             ++off) {
            MoveToEx(dc, r.left, r.bottom - 1 + off, nullptr);
            LineTo(dc, r.left + corner_w, r.bottom - 1 + off);
            MoveToEx(dc, r.left + off, r.bottom - corner_h, nullptr);
            LineTo(dc, r.left + off, r.bottom + kCornerHighlightOutsetPx);
        }
        break;
    case greenflame::core::SelectionHandle::BottomRight:
        for (int off = -kCornerHighlightOutsetPx; off <= kCornerHighlightOutsetPx;
             ++off) {
            MoveToEx(dc, r.right - corner_w, r.bottom - 1 + off, nullptr);
            LineTo(dc, r.right, r.bottom - 1 + off);
            MoveToEx(dc, r.right - 1 + off, r.bottom - corner_h, nullptr);
            LineTo(dc, r.right - 1 + off, r.bottom + kCornerHighlightOutsetPx);
        }
        break;
    }

    SelectObject(dc, old_pen);
}

void Draw_selection_dim_and_border(HDC buf_dc, HBITMAP buf_bmp, int w, int h,
                                   greenflame::core::RectPx const &sel,
                                   std::span<uint8_t> pixels,
                                   greenflame::PaintResources const *res, bool dashed) {
    if (sel.Is_empty()) return;
    int const row_bytes = greenflame::Row_bytes32(w);
    size_t const size = static_cast<size_t>(row_bytes) * static_cast<size_t>(h);
    if (pixels.size() < size) return;
    BITMAPINFOHEADER bmi;
    greenflame::Fill_bmi32_top_down(bmi, w, h);
    if (GetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                  reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS) != 0) {
        greenflame::core::Dim_pixels_outside_rect(pixels, w, h, row_bytes, sel);
        SetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                  reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS);
    }
    if (res && res->handle_pen) {
        if (dashed) {
            Draw_dashed_rect_border(buf_dc, res->handle_pen, sel.left, sel.top,
                                    sel.right, sel.bottom);
        } else {
            HGDIOBJ old_pen = SelectObject(buf_dc, res->handle_pen);
            HGDIOBJ old_brush = SelectObject(buf_dc, GetStockObject(NULL_BRUSH));
            Rectangle(buf_dc, sel.left, sel.top, sel.right, sel.bottom);
            SelectObject(buf_dc, old_brush);
            SelectObject(buf_dc, old_pen);
        }
    }
}

void Draw_annotations_to_buffer(
    HDC buf_dc, HBITMAP buf_bmp, int w, int h, std::span<uint8_t> pixels,
    std::span<const greenflame::core::Annotation> annotations) {
    if (annotations.empty() || w <= 0 || h <= 0) {
        return;
    }
    int const row_bytes = greenflame::Row_bytes32(w);
    size_t const size = static_cast<size_t>(row_bytes) * static_cast<size_t>(h);
    if (pixels.size() < size) {
        return;
    }
    BITMAPINFOHEADER bmi{};
    greenflame::Fill_bmi32_top_down(bmi, w, h);
    if (GetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                  reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS) == 0) {
        return;
    }

    greenflame::core::RectPx const target_bounds =
        greenflame::core::RectPx::From_ltrb(0, 0, w, h);
    greenflame::core::Blend_annotations_onto_pixels(pixels, w, h, row_bytes,
                                                    annotations, target_bounds);
    SetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
              reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS);
}

void Draw_draft_freehand_stroke(HDC dc,
                                std::span<const greenflame::core::PointPx> points,
                                greenflame::core::StrokeStyle style) {
    if (dc == nullptr || points.empty()) {
        return;
    }

    int const width_px = std::max<int32_t>(1, style.width_px);
    HBRUSH const brush = CreateSolidBrush(style.color);

    LOGBRUSH log_brush{};
    log_brush.lbStyle = BS_SOLID;
    log_brush.lbColor = style.color;
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
                            static_cast<DWORD>(width_px), &log_brush, 0, nullptr);
    if (pen == nullptr) {
        pen = CreatePen(PS_SOLID, width_px, style.color);
    }
    if (pen == nullptr) {
        if (brush != nullptr) {
            DeleteObject(brush);
        }
        return;
    }

    HGDIOBJ const old_pen = SelectObject(dc, pen);
    HGDIOBJ const old_brush =
        SelectObject(dc, brush != nullptr ? brush : GetStockObject(NULL_BRUSH));

    if (points.size() == 1) {
        int const left = points.front().x - width_px / 2;
        int const top = points.front().y - width_px / 2;
        Ellipse(dc, left, top, left + width_px, top + width_px);
    } else {
        std::vector<POINT> polyline_points;
        polyline_points.reserve(points.size());
        for (greenflame::core::PointPx const point : points) {
            polyline_points.push_back({point.x, point.y});
        }
        if (!polyline_points.empty()) {
            Polyline(dc, polyline_points.data(),
                     static_cast<int>(polyline_points.size()));
        }
    }

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
    if (brush != nullptr) {
        DeleteObject(brush);
    }
}

void Draw_annotation_selection_corners(HDC dc, HPEN pen,
                                       greenflame::core::RectPx const &bounds) {
    if (pen == nullptr || bounds.Is_empty()) {
        return;
    }
    greenflame::core::RectPx const r = bounds.Normalized();
    int const corner_w = std::min(greenflame::core::kMaxCornerSizePx, r.Width() / 2);
    int const corner_h = std::min(greenflame::core::kMaxCornerSizePx, r.Height() / 2);
    HGDIOBJ const old_pen = SelectObject(dc, pen);

    MoveToEx(dc, r.left, r.top + corner_h, nullptr);
    LineTo(dc, r.left, r.top);
    LineTo(dc, r.left + corner_w, r.top);

    MoveToEx(dc, r.right - 1 - corner_w, r.top, nullptr);
    LineTo(dc, r.right - 1, r.top);
    LineTo(dc, r.right - 1, r.top + corner_h);

    MoveToEx(dc, r.left, r.bottom - 1 - corner_h, nullptr);
    LineTo(dc, r.left, r.bottom - 1);
    LineTo(dc, r.left + corner_w, r.bottom - 1);

    MoveToEx(dc, r.right - 1 - corner_w, r.bottom - 1, nullptr);
    LineTo(dc, r.right - 1, r.bottom - 1);
    LineTo(dc, r.right - 1, r.bottom - 1 - corner_h);

    SelectObject(dc, old_pen);
}

void Draw_toolbar_tooltip(HDC buf_dc, HBITMAP buf_bmp, int w, int h,
                          std::span<uint8_t> pixels,
                          greenflame::PaintResources const *res, std::wstring_view text,
                          std::optional<greenflame::core::RectPx> anchor_bounds) {
    if (text.empty() || !anchor_bounds.has_value() || res == nullptr ||
        res->font_dim == nullptr || w <= 0 || h <= 0) {
        return;
    }

    std::wstring const text_str(text);
    SIZE text_size{};
    Measure_text_size(buf_dc, res->font_dim, text_str, text_size);
    int const box_w = text_size.cx + 2 * kToolbarTooltipMarginPx;
    int const box_h = text_size.cy + 2 * kToolbarTooltipMarginPx;
    int const anchor_center = anchor_bounds->left + anchor_bounds->Width() / 2;
    int box_left = anchor_center - box_w / 2;
    int box_top = anchor_bounds->top - kToolbarTooltipOffsetPx - box_h;
    if (box_top < 0) {
        box_top = anchor_bounds->bottom + kToolbarTooltipOffsetPx;
    }
    if (box_left < 0) {
        box_left = 0;
    }
    if (box_left + box_w > w) {
        box_left = w - box_w;
    }
    if (box_top < 0) {
        box_top = 0;
    }
    if (box_top + box_h > h) {
        box_top = h - box_h;
    }

    greenflame::core::RectPx const tooltip_rect = greenflame::core::RectPx::From_ltrb(
        box_left, box_top, box_left + box_w, box_top + box_h);
    int const row_bytes = greenflame::Row_bytes32(w);
    size_t const size = static_cast<size_t>(row_bytes) * static_cast<size_t>(h);
    if (pixels.size() >= size) {
        BITMAPINFOHEADER bmi{};
        greenflame::Fill_bmi32_top_down(bmi, w, h);
        if (GetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                      reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS) != 0) {
            greenflame::core::Blend_rect_onto_pixels(
                pixels, w, h, row_bytes, tooltip_rect, greenflame::kCoordTooltipBg,
                greenflame::kCoordTooltipAlpha);
            SetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                      reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS);
        }
    }

    if (res->border_pen != nullptr) {
        HGDIOBJ const old_pen = SelectObject(buf_dc, res->border_pen);
        SelectObject(buf_dc, GetStockObject(NULL_BRUSH));
        Rectangle(buf_dc, box_left, box_top, box_left + box_w, box_top + box_h);
        SelectObject(buf_dc, old_pen);
    }

    HGDIOBJ const old_font = SelectObject(buf_dc, res->font_dim);
    SetBkMode(buf_dc, TRANSPARENT);
    SetTextColor(buf_dc, greenflame::kCoordTooltipText);
    RECT text_rect = {box_left + kToolbarTooltipMarginPx,
                      box_top + kToolbarTooltipMarginPx,
                      box_left + box_w - kToolbarTooltipMarginPx,
                      box_top + box_h - kToolbarTooltipMarginPx};
    DrawTextW(buf_dc, text_str.c_str(), -1, &text_rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(buf_dc, old_font);
}

[[nodiscard]] bool Rect_fully_covered_by_monitors(
    greenflame::core::RectPx rect,
    std::span<const greenflame::core::RectPx> monitor_rects_client) {
    greenflame::core::RectPx const target = rect.Normalized();
    if (target.Is_empty() || monitor_rects_client.empty()) {
        return false;
    }

    HRGN target_region =
        CreateRectRgn(target.left, target.top, target.right, target.bottom);
    if (!target_region) {
        return false;
    }
    HRGN covered_region = CreateRectRgn(0, 0, 0, 0);
    if (!covered_region) {
        DeleteObject(target_region);
        return false;
    }

    bool any_monitor_region = false;
    for (greenflame::core::RectPx const &monitor_rect : monitor_rects_client) {
        greenflame::core::RectPx const mon = monitor_rect.Normalized();
        if (mon.Is_empty()) {
            continue;
        }
        HRGN mon_region = CreateRectRgn(mon.left, mon.top, mon.right, mon.bottom);
        if (!mon_region) {
            continue;
        }
        any_monitor_region = true;
        (void)CombineRgn(covered_region, covered_region, mon_region, RGN_OR);
        DeleteObject(mon_region);
    }
    if (!any_monitor_region) {
        DeleteObject(covered_region);
        DeleteObject(target_region);
        return false;
    }

    HRGN uncovered_region = CreateRectRgn(0, 0, 0, 0);
    if (!uncovered_region) {
        DeleteObject(covered_region);
        DeleteObject(target_region);
        return false;
    }
    int const diff_type =
        CombineRgn(uncovered_region, target_region, covered_region, RGN_DIFF);

    DeleteObject(uncovered_region);
    DeleteObject(covered_region);
    DeleteObject(target_region);
    return diff_type == NULLREGION;
}

struct DimLabelPositions {
    bool draw_side;
    bool draw_center_box;
    bool draw_help_box;
    int width_box_left, width_box_top, width_box_w, width_box_h;
    int height_box_left, height_box_top, height_box_w, height_box_h;
    int center_box_left, center_box_top, center_box_w, center_box_h;
    int help_box_left, help_box_top, help_box_w, help_box_h;
    greenflame::core::RectPx width_box_rect, height_box_rect, center_box_rect,
        help_box_rect;
};

constexpr int kDimMargin = 4;
constexpr int kDimGap = 4;
constexpr int kCenterMinPadding = 24;

void Measure_text_size(HDC dc, HFONT font, std::wstring const &text, SIZE &text_size) {
    text_size = {};
    if (font == nullptr) {
        return;
    }
    HGDIOBJ const old_font = SelectObject(dc, font);
    (void)GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()),
                                &text_size);
    SelectObject(dc, old_font);
}

void Compute_side_label_positions(DimLabelPositions &p, HDC dc,
                                  greenflame::core::RectPx const &sel, int w, int h,
                                  int center_x, int center_y,
                                  std::wstring const &width_str,
                                  std::wstring const &height_str, HFONT font_dim) {
    if (font_dim == nullptr) {
        p.draw_side = false;
        return;
    }
    p.draw_side = true;

    SIZE width_size = {};
    SIZE height_size = {};
    Measure_text_size(dc, font_dim, width_str, width_size);
    Measure_text_size(dc, font_dim, height_str, height_size);

    p.width_box_w = width_size.cx + 2 * kDimMargin;
    p.width_box_h = width_size.cy + 2 * kDimMargin;
    p.height_box_w = height_size.cx + 2 * kDimMargin;
    p.height_box_h = height_size.cy + 2 * kDimMargin;

    p.width_box_left = center_x - p.width_box_w / 2;
    bool const above_fits = (sel.top - p.width_box_h - kDimGap >= 0);
    bool const below_fits = (sel.bottom + kDimGap + p.width_box_h <= h);
    if (above_fits) {
        p.width_box_top = sel.top - p.width_box_h - kDimGap;
    } else if (below_fits) {
        p.width_box_top = sel.bottom + kDimGap;
    } else {
        p.width_box_top = (sel.top - p.width_box_h - kDimGap >= h - p.width_box_h)
                              ? (sel.bottom + kDimGap)
                              : (sel.top - p.width_box_h - kDimGap);
    }
    if (p.width_box_top < 0) p.width_box_top = 0;
    if (p.width_box_top + p.width_box_h > h) p.width_box_top = h - p.width_box_h;

    p.height_box_top = center_y - p.height_box_h / 2;
    int const height_box_left_candidate = sel.left - kDimGap - p.height_box_w;
    int const height_box_right_candidate = sel.right + kDimGap;
    bool const left_fits = (height_box_left_candidate >= 0);
    bool const right_fits = (height_box_right_candidate + p.height_box_w <= w);
    if (left_fits) {
        p.height_box_left = height_box_left_candidate;
    } else if (right_fits) {
        p.height_box_left = height_box_right_candidate;
    } else {
        p.height_box_left = (height_box_left_candidate >= w - p.height_box_w)
                                ? height_box_right_candidate
                                : height_box_left_candidate;
    }
    if (p.height_box_left < 0) p.height_box_left = 0;
    if (p.height_box_left + p.height_box_w > w) {
        p.height_box_left = w - p.height_box_w;
    }

    p.width_box_rect = greenflame::core::RectPx::From_ltrb(
        p.width_box_left, p.width_box_top, p.width_box_left + p.width_box_w,
        p.width_box_top + p.width_box_h);
    p.height_box_rect = greenflame::core::RectPx::From_ltrb(
        p.height_box_left, p.height_box_top, p.height_box_left + p.height_box_w,
        p.height_box_top + p.height_box_h);
}

void Compute_center_label_position(DimLabelPositions &p, HDC dc,
                                   greenflame::core::RectPx const &sel, int center_x,
                                   int center_y, std::wstring const &center_str,
                                   HFONT font_center) {
    if (font_center == nullptr) {
        p.draw_center_box = false;
        return;
    }

    SIZE center_size = {};
    Measure_text_size(dc, font_center, center_str, center_size);
    p.center_box_w = center_size.cx + 2 * kDimMargin;
    p.center_box_h = center_size.cy + 2 * kDimMargin;
    p.center_box_left = center_x - p.center_box_w / 2;
    p.center_box_top = center_y - p.center_box_h / 2;
    p.draw_center_box = (sel.Width() >= p.center_box_w + 2 * kCenterMinPadding &&
                         sel.Height() >= p.center_box_h + 2 * kCenterMinPadding);
    if (p.draw_center_box) {
        p.center_box_rect = greenflame::core::RectPx::From_ltrb(
            p.center_box_left, p.center_box_top, p.center_box_left + p.center_box_w,
            p.center_box_top + p.center_box_h);
    }
}

void Compute_help_label_position(
    DimLabelPositions &p, HDC dc, greenflame::core::RectPx const &sel, int w, int h,
    std::wstring const &help_str, HFONT font_help_hint,
    std::span<const greenflame::core::RectPx> monitor_rects_client) {
    if (font_help_hint == nullptr) {
        p.draw_help_box = false;
        return;
    }

    SIZE help_size = {};
    Measure_text_size(dc, font_help_hint, help_str, help_size);
    p.help_box_w = help_size.cx + 2 * kDimMargin;
    p.help_box_h = help_size.cy + 2 * kDimMargin;
    bool const selection_wide_enough = (sel.Width() >= p.help_box_w);
    p.help_box_left = sel.right - p.help_box_w;
    greenflame::core::RectPx const below_candidate =
        greenflame::core::RectPx::From_ltrb(p.help_box_left, sel.bottom + kDimGap,
                                            p.help_box_left + p.help_box_w,
                                            sel.bottom + kDimGap + p.help_box_h);
    greenflame::core::RectPx const above_candidate =
        greenflame::core::RectPx::From_ltrb(
            p.help_box_left, sel.top - kDimGap - p.help_box_h,
            p.help_box_left + p.help_box_w, sel.top - kDimGap);

    auto const box_fits = [&](greenflame::core::RectPx const &candidate) {
        if (!monitor_rects_client.empty()) {
            return Rect_fully_covered_by_monitors(candidate, monitor_rects_client);
        }
        greenflame::core::RectPx const normalized = candidate.Normalized();
        return normalized.left >= 0 && normalized.top >= 0 && normalized.right <= w &&
               normalized.bottom <= h;
    };

    bool const fits_below = box_fits(below_candidate);
    bool const fits_above = box_fits(above_candidate);

    p.draw_help_box = selection_wide_enough && (fits_below || fits_above);
    if (p.draw_help_box && fits_below) {
        p.help_box_rect = below_candidate;
        p.help_box_left = p.help_box_rect.left;
        p.help_box_top = p.help_box_rect.top;
    } else if (p.draw_help_box) {
        p.help_box_rect = above_candidate;
        p.help_box_left = p.help_box_rect.left;
        p.help_box_top = p.help_box_rect.top;
    }
}

static DimLabelPositions Compute_dim_label_positions(
    HDC dc, greenflame::core::RectPx const &sel, int w, int h,
    std::wstring const &width_str, std::wstring const &height_str,
    std::wstring const &center_str, std::wstring const &help_str, HFONT font_dim,
    HFONT font_center, HFONT font_help_hint,
    std::span<const greenflame::core::RectPx> monitor_rects_client) {
    DimLabelPositions p{};
    int const center_x = (sel.left + sel.right) / 2;
    int const center_y = (sel.top + sel.bottom) / 2;
    Compute_side_label_positions(p, dc, sel, w, h, center_x, center_y, width_str,
                                 height_str, font_dim);
    Compute_center_label_position(p, dc, sel, center_x, center_y, center_str,
                                  font_center);
    Compute_help_label_position(p, dc, sel, w, h, help_str, font_help_hint,
                                monitor_rects_client);
    return p;
}

void Draw_dimension_labels(
    HDC buf_dc, HBITMAP buf_bmp, int w, int h, greenflame::core::RectPx const &sel,
    std::span<const greenflame::core::RectPx> monitor_rects_client,
    std::span<uint8_t> pixels, greenflame::PaintResources const *res,
    bool show_selection_size_side_labels, bool show_selection_size_center_label,
    bool show_help_hint_requested) {
    constexpr int k_dim_margin = 4;
    constexpr wchar_t k_help_hint_text[] = L"Ctrl-H for Help";
    bool const show_help_hint =
        show_help_hint_requested && (res != nullptr && res->font_help_hint != nullptr);
    if (!show_selection_size_side_labels && !show_selection_size_center_label &&
        !show_help_hint) {
        return;
    }
    int const row_bytes = greenflame::Row_bytes32(w);
    size_t const pix_size = static_cast<size_t>(row_bytes) * static_cast<size_t>(h);
    if (pixels.size() < pix_size) return;
    BITMAPINFOHEADER bmi_dim;
    greenflame::Fill_bmi32_top_down(bmi_dim, w, h);
    if (GetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                  reinterpret_cast<BITMAPINFO *>(&bmi_dim), DIB_RGB_COLORS) != 0) {
        HFONT font_dim =
            show_selection_size_side_labels ? (res ? res->font_dim : nullptr) : nullptr;
        HFONT font_center = show_selection_size_center_label
                                ? (res ? res->font_center : nullptr)
                                : nullptr;
        HFONT font_help_hint = show_help_hint ? res->font_help_hint : nullptr;
        if (!font_dim && !font_center && !font_help_hint) return;

        HFONT const base_font =
            font_dim ? font_dim : (font_center ? font_center : font_help_hint);
        HGDIOBJ old_font = SelectObject(buf_dc, base_font);

        int const sel_w = sel.Width();
        int const sel_h = sel.Height();
        std::wstring const width_str = std::to_wstring(sel_w);
        std::wstring const height_str = std::to_wstring(sel_h);
        std::wstring const center_str = width_str + L" x " + height_str;
        std::wstring const help_str = k_help_hint_text;

        DimLabelPositions const p = Compute_dim_label_positions(
            buf_dc, sel, w, h, width_str, height_str, center_str, help_str, font_dim,
            font_center, font_help_hint, monitor_rects_client);
        SelectObject(buf_dc, base_font);

        // Pixel blending for box backgrounds.
        constexpr unsigned char box_alpha = greenflame::kCoordTooltipAlpha;
        bool wrote_pixels = false;
        if (p.draw_side) {
            greenflame::core::Blend_rect_onto_pixels(
                pixels, w, h, row_bytes, p.width_box_rect, greenflame::kCoordTooltipBg,
                box_alpha);
            greenflame::core::Blend_rect_onto_pixels(
                pixels, w, h, row_bytes, p.height_box_rect, greenflame::kCoordTooltipBg,
                box_alpha);
            wrote_pixels = true;
        }
        if (p.draw_center_box) {
            greenflame::core::Blend_rect_onto_pixels(
                pixels, w, h, row_bytes, p.center_box_rect, greenflame::kCoordTooltipBg,
                box_alpha);
            wrote_pixels = true;
        }
        if (p.draw_help_box) {
            greenflame::core::Blend_rect_onto_pixels(
                pixels, w, h, row_bytes, p.help_box_rect, greenflame::kCoordTooltipBg,
                box_alpha);
            wrote_pixels = true;
        }
        if (wrote_pixels) {
            SetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                      reinterpret_cast<BITMAPINFO *>(&bmi_dim), DIB_RGB_COLORS);
        }

        HPEN dark_pen = CreatePen(PS_SOLID, 1, greenflame::kCoordTooltipText);
        if (dark_pen) {
            HGDIOBJ old_dim_pen = SelectObject(buf_dc, dark_pen);
            SelectObject(buf_dc, GetStockObject(NULL_BRUSH));
            if (p.draw_side) {
                Rectangle(buf_dc, p.width_box_left, p.width_box_top,
                          p.width_box_left + p.width_box_w,
                          p.width_box_top + p.width_box_h);
                Rectangle(buf_dc, p.height_box_left, p.height_box_top,
                          p.height_box_left + p.height_box_w,
                          p.height_box_top + p.height_box_h);
            }
            if (p.draw_center_box) {
                Rectangle(buf_dc, p.center_box_left, p.center_box_top,
                          p.center_box_left + p.center_box_w,
                          p.center_box_top + p.center_box_h);
            }
            if (p.draw_help_box) {
                Rectangle(buf_dc, p.help_box_left, p.help_box_top,
                          p.help_box_left + p.help_box_w,
                          p.help_box_top + p.help_box_h);
            }
            SelectObject(buf_dc, old_dim_pen);
            DeleteObject(dark_pen);
        }

        SetBkMode(buf_dc, TRANSPARENT);
        SetTextColor(buf_dc, greenflame::kCoordTooltipText);
        if (p.draw_side) {
            RECT width_text_rc = {p.width_box_left + k_dim_margin,
                                  p.width_box_top + k_dim_margin,
                                  p.width_box_left + p.width_box_w - k_dim_margin,
                                  p.width_box_top + p.width_box_h - k_dim_margin};
            RECT height_text_rc = {p.height_box_left + k_dim_margin,
                                   p.height_box_top + k_dim_margin,
                                   p.height_box_left + p.height_box_w - k_dim_margin,
                                   p.height_box_top + p.height_box_h - k_dim_margin};
            SelectObject(buf_dc, font_dim);
            DrawTextW(buf_dc, width_str.c_str(), -1, &width_text_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextW(buf_dc, height_str.c_str(), -1, &height_text_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (p.draw_center_box) {
            RECT center_text_rc = {p.center_box_left + k_dim_margin,
                                   p.center_box_top + k_dim_margin,
                                   p.center_box_left + p.center_box_w - k_dim_margin,
                                   p.center_box_top + p.center_box_h - k_dim_margin};
            SelectObject(buf_dc, font_center);
            DrawTextW(buf_dc, center_str.c_str(), -1, &center_text_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (p.draw_help_box) {
            RECT help_text_rc = {p.help_box_left + k_dim_margin,
                                 p.help_box_top + k_dim_margin,
                                 p.help_box_left + p.help_box_w - k_dim_margin,
                                 p.help_box_top + p.help_box_h - k_dim_margin};
            SelectObject(buf_dc, font_help_hint);
            DrawTextW(buf_dc, help_str.c_str(), -1, &help_text_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(buf_dc, old_font);
    }
}

void Draw_transient_center_label(HDC buf_dc, HBITMAP buf_bmp, int w, int h,
                                 greenflame::core::RectPx const &sel,
                                 std::span<uint8_t> pixels,
                                 greenflame::PaintResources const *res,
                                 std::wstring_view text) {
    if (sel.Is_empty() || text.empty() || res == nullptr || res->font_center == nullptr) {
        return;
    }

    int const row_bytes = greenflame::Row_bytes32(w);
    size_t const pix_size = static_cast<size_t>(row_bytes) * static_cast<size_t>(h);
    if (pixels.size() < pix_size) {
        return;
    }

    std::wstring const label(text);
    HGDIOBJ const old_font = SelectObject(buf_dc, res->font_center);

    DimLabelPositions positions{};
    int const center_x = (sel.left + sel.right) / 2;
    int const center_y = (sel.top + sel.bottom) / 2;
    Compute_center_label_position(positions, buf_dc, sel, center_x, center_y, label,
                                  res->font_center);
    SelectObject(buf_dc, old_font);
    if (!positions.draw_center_box) {
        return;
    }

    BITMAPINFOHEADER bmi{};
    greenflame::Fill_bmi32_top_down(bmi, w, h);
    if (GetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                  reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS) == 0) {
        return;
    }

    greenflame::core::Blend_rect_onto_pixels(
        pixels, w, h, row_bytes, positions.center_box_rect, greenflame::kCoordTooltipBg,
        greenflame::kCoordTooltipAlpha);
    SetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
              reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS);

    HPEN const border_pen = CreatePen(PS_SOLID, 1, greenflame::kCoordTooltipText);
    if (border_pen != nullptr) {
        HGDIOBJ const old_pen = SelectObject(buf_dc, border_pen);
        HGDIOBJ const old_brush = SelectObject(buf_dc, GetStockObject(NULL_BRUSH));
        Rectangle(buf_dc, positions.center_box_left, positions.center_box_top,
                  positions.center_box_left + positions.center_box_w,
                  positions.center_box_top + positions.center_box_h);
        SelectObject(buf_dc, old_brush);
        SelectObject(buf_dc, old_pen);
        DeleteObject(border_pen);
    }

    RECT text_rect = {positions.center_box_left + kDimMargin,
                      positions.center_box_top + kDimMargin,
                      positions.center_box_left + positions.center_box_w - kDimMargin,
                      positions.center_box_top + positions.center_box_h - kDimMargin};
    SetBkMode(buf_dc, TRANSPARENT);
    SetTextColor(buf_dc, greenflame::kCoordTooltipText);
    HGDIOBJ const old_center_font = SelectObject(buf_dc, res->font_center);
    DrawTextW(buf_dc, label.c_str(), -1, &text_rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(buf_dc, old_center_font);
}

static void Draw_magnifier(HDC buf_dc, HBITMAP buf_bmp, int w, int h, int cx, int cy,
                           int mon_left, int mon_top, int mon_right, int mon_bottom,
                           std::span<uint8_t> pixels,
                           greenflame::PaintResources const *res,
                           greenflame::GdiCaptureResult const *capture) {
    constexpr int k_magnifier_half_zoom = kMagnifierZoom / 2;
    int const src_x = cx - kMagnifierSource / 2;
    int const src_y = cy - kMagnifierSource / 2;
    int const src_right = src_x + kMagnifierSource + 1;
    int const src_bottom = src_y + kMagnifierSource + 1;
    int const crosshair_left = (cx - src_x) * kMagnifierZoom - k_magnifier_half_zoom;
    int const crosshair_top = (cy - src_y) * kMagnifierZoom - k_magnifier_half_zoom;
    // Clamp sample to the full capture (virtual desktop) extent so that pixels
    // from adjacent monitors are shown rather than producing a checkered gap.
    // Monitor bounds are only used for magnifier *placement* below.
    int const sample_left = std::max(src_x, 0);
    int const sample_top = std::max(src_y, 0);
    int const sample_right = std::min(src_right, w);
    int const sample_bottom = std::min(src_bottom, h);
    bool const source_has_coverage =
        sample_left < sample_right && sample_top < sample_bottom;

    int mag_left = 0, mag_top = 0;
    int const pad = kMagnifierPadding;
    struct {
        int dx, dy;
    } constexpr quadrants[] = {
        {+pad, +pad},                                   // bottom-right
        {-pad - kMagnifierSize, +pad},                  // bottom-left
        {+pad, -pad - kMagnifierSize},                  // top-right
        {-pad - kMagnifierSize, -pad - kMagnifierSize}, // top-left
    };
    bool placed = false;
    for (auto const &q : quadrants) {
        int const ml = cx + q.dx;
        int const mt = cy + q.dy;
        if (ml >= mon_left && mt >= mon_top && ml + kMagnifierSize <= mon_right &&
            mt + kMagnifierSize <= mon_bottom) {
            mag_left = ml;
            mag_top = mt;
            placed = true;
            break;
        }
    }
    if (!placed) {
        int const max_left = std::max(mon_left, mon_right - kMagnifierSize);
        int const max_top = std::max(mon_top, mon_bottom - kMagnifierSize);
        mag_left = std::clamp(cx + pad, mon_left, max_left);
        mag_top = std::clamp(cy + pad, mon_top, max_top);
    }

    // Draw magnifier from capture so dotted crosshair is not magnified
    // (Greenshot does the same). Out-of-monitor sample area is checkered.
    HRGN rgn = CreateEllipticRgn(mag_left, mag_top, mag_left + kMagnifierSize,
                                 mag_top + kMagnifierSize);
    if (rgn) {
        SelectClipRgn(buf_dc, rgn);
        Fill_magnifier_checkerboard(buf_dc, mag_left, mag_top, src_x, src_y,
                                    -k_magnifier_half_zoom, -k_magnifier_half_zoom);
        if (source_has_coverage) {
            int const dst_left = mag_left + (sample_left - src_x) * kMagnifierZoom -
                                 k_magnifier_half_zoom;
            int const dst_top =
                mag_top + (sample_top - src_y) * kMagnifierZoom - k_magnifier_half_zoom;
            int const dst_w = (sample_right - sample_left) * kMagnifierZoom;
            int const dst_h = (sample_bottom - sample_top) * kMagnifierZoom;
            SetStretchBltMode(buf_dc, COLORONCOLOR);
            if (capture && capture->Is_valid()) {
                HDC capture_dc = CreateCompatibleDC(buf_dc);
                if (capture_dc) {
                    HGDIOBJ old_cap = SelectObject(capture_dc, capture->bitmap);
                    StretchBlt(buf_dc, dst_left, dst_top, dst_w, dst_h, capture_dc,
                               sample_left, sample_top, sample_right - sample_left,
                               sample_bottom - sample_top, SRCCOPY);
                    SelectObject(capture_dc, old_cap);
                    DeleteDC(capture_dc);
                }
            } else {
                StretchBlt(buf_dc, dst_left, dst_top, dst_w, dst_h, buf_dc, sample_left,
                           sample_top, sample_right - sample_left,
                           sample_bottom - sample_top, SRCCOPY);
            }
        }
        SelectClipRgn(buf_dc, nullptr);
        COLORREF const border_color = Get_pen_color_or_fallback(
            res ? res->border_pen : nullptr, greenflame::kBorderColor);
        Draw_antialiased_magnifier_border(buf_dc, mag_left, mag_top, border_color);
        DeleteObject(rgn);
    }

    // DIB round-trip to blend crosshair pixels inside the magnifier circle.
    int const row_bytes = greenflame::Row_bytes32(w);
    size_t const pix_size = static_cast<size_t>(row_bytes) * static_cast<size_t>(h);
    if (pixels.size() >= pix_size) {
        BITMAPINFOHEADER bmi;
        greenflame::Fill_bmi32_top_down(bmi, w, h);
        if (GetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                      reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS) != 0) {
            Blend_magnifier_crosshair_onto_pixels(pixels, w, h, row_bytes, mag_left,
                                                  mag_top, crosshair_left,
                                                  crosshair_top);
            SetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                      reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS);
        }
    }
}

void Draw_crosshair_and_coord_tooltip(HDC buf_dc, HBITMAP buf_bmp, HWND hwnd, int w,
                                      int h, int cx, int cy, std::span<uint8_t> pixels,
                                      greenflame::PaintResources const *res,
                                      greenflame::GdiCaptureResult const *capture) {
    if (cx < 0 || cx >= w || cy < 0 || cy >= h) return;

    // Monitor bounds in client coords (for coord tooltip and magnifier).
    POINT screen_pt;
    GetCursorPos(&screen_pt);
    HMONITOR h_mon = MonitorFromPoint(screen_pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    int mon_left = 0, mon_top = 0, mon_right = w, mon_bottom = h;
    if (GetMonitorInfoW(h_mon, &mi)) {
        RECT const &r = mi.rcMonitor;
        POINT tl = {r.left, r.top}, br = {r.right, r.bottom};
        ScreenToClient(hwnd, &tl);
        ScreenToClient(hwnd, &br);
        mon_left = tl.x;
        mon_top = tl.y;
        mon_right = br.x;
        mon_bottom = br.y;
    }

    HPEN cross_pen = res ? res->crosshair_pen : nullptr;
    if (cross_pen) {
        HGDIOBJ old_pen = SelectObject(buf_dc, cross_pen);
        for (int y = 0; y < h; y += 2) {
            MoveToEx(buf_dc, cx, y, nullptr);
            LineTo(buf_dc, cx, y + 1);
        }
        for (int x = 0; x < w; x += 2) {
            MoveToEx(buf_dc, x, cy, nullptr);
            LineTo(buf_dc, x + 1, cy);
        }
        SelectObject(buf_dc, old_pen);
    }

    // Round magnifier (drawn before coord tooltip so coords stay on top).
    Draw_magnifier(buf_dc, buf_bmp, w, h, cx, cy, mon_left, mon_top, mon_right,
                   mon_bottom, pixels, res, capture);

    // Compute coord tooltip position.
    constexpr int k_coord_padding = 4;
    constexpr int k_coord_margin = 4;
    std::wstring coord_str = std::to_wstring(cx) + L" x " + std::to_wstring(cy);
    HFONT font = res ? res->font_dim : nullptr;
    bool tooltip_ready = false;
    int box_w = 0, box_h = 0, tt_left = 0, tt_top = 0;
    greenflame::core::RectPx box_rect;
    HGDIOBJ old_font = nullptr;
    if (font) {
        old_font = SelectObject(buf_dc, font);
        SIZE text_size = {};
        if (GetTextExtentPoint32W(buf_dc, coord_str.c_str(),
                                  static_cast<int>(coord_str.size()), &text_size)) {
            box_w = text_size.cx + 2 * k_coord_margin;
            box_h = text_size.cy + 2 * k_coord_margin;
            tt_left = cx + k_coord_padding;
            tt_top = cy + k_coord_padding;
            if (tt_left + box_w > mon_right) tt_left = cx - k_coord_padding - box_w;
            if (tt_top + box_h > mon_bottom) tt_top = cy - k_coord_padding - box_h;
            if (tt_left < mon_left) tt_left = mon_left;
            if (tt_top < mon_top) tt_top = mon_top;
            box_rect = greenflame::core::RectPx::From_ltrb(
                tt_left, tt_top, tt_left + box_w, tt_top + box_h);
            tooltip_ready = true;
        }
    }

    // DIB round-trip to blend tooltip background.
    int const row_bytes = greenflame::Row_bytes32(w);
    size_t const pix_size = static_cast<size_t>(row_bytes) * static_cast<size_t>(h);
    if (tooltip_ready && pixels.size() >= pix_size) {
        BITMAPINFOHEADER bmi;
        greenflame::Fill_bmi32_top_down(bmi, w, h);
        if (GetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                      reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS) != 0) {
            greenflame::core::Blend_rect_onto_pixels(pixels, w, h, row_bytes, box_rect,
                                                     greenflame::kCoordTooltipBg,
                                                     greenflame::kCoordTooltipAlpha);
            SetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                      reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS);
        }
    }

    // GDI tooltip border + text (after SetDIBits).
    if (tooltip_ready) {
        HPEN border_pen = res ? res->border_pen : nullptr;
        if (border_pen) {
            HGDIOBJ old_pen = SelectObject(buf_dc, border_pen);
            SelectObject(buf_dc, GetStockObject(NULL_BRUSH));
            Rectangle(buf_dc, tt_left, tt_top, tt_left + box_w, tt_top + box_h);
            SelectObject(buf_dc, old_pen);
        }
        SetBkMode(buf_dc, TRANSPARENT);
        SetTextColor(buf_dc, greenflame::kCoordTooltipText);
        RECT text_rc = {tt_left + k_coord_margin, tt_top + k_coord_margin,
                        tt_left + box_w - k_coord_margin,
                        tt_top + box_h - k_coord_margin};
        DrawTextW(buf_dc, coord_str.c_str(), -1, &text_rc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (old_font) SelectObject(buf_dc, old_font);
}

} // namespace

namespace greenflame {

void Paint_overlay(HDC hdc, HWND hwnd, const RECT &rc, const PaintOverlayInput &in) {
    int const w = rc.right - rc.left;
    int const h = rc.bottom - rc.top;

    if (!in.capture || !in.capture->Is_valid() || w <= 0 || h <= 0) {
        HBRUSH brush = CreateSolidBrush(RGB(0x40, 0x40, 0x40));
        if (brush) {
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
        }
        return;
    }

    HDC buf_dc = CreateCompatibleDC(hdc);
    HBITMAP buf_bmp = CreateCompatibleBitmap(hdc, w, h);
    if (!buf_dc || !buf_bmp) {
        if (buf_bmp) DeleteObject(buf_bmp);
        if (buf_dc) DeleteDC(buf_dc);
        return;
    }

    HGDIOBJ old_buf = SelectObject(buf_dc, buf_bmp);

    Draw_capture_to_buffer(buf_dc, hdc, w, h, in.capture);

    greenflame::core::RectPx sel =
        (in.modifier_preview || in.handle_dragging || in.dragging || in.move_dragging)
            ? in.live_rect
            : in.final_selection;
    Draw_selection_dim_and_border(buf_dc, buf_bmp, w, h, sel, in.paint_buffer,
                                  in.resources, in.dragging);
    Draw_annotations_to_buffer(buf_dc, buf_bmp, w, h, in.paint_buffer, in.annotations);
    if (in.draft_freehand_style.has_value()) {
        Draw_draft_freehand_stroke(buf_dc, in.draft_freehand_points,
                                   *in.draft_freehand_style);
    }

    bool const interacting =
        in.dragging || in.handle_dragging || in.move_dragging || in.modifier_preview;
    if (interacting && !sel.Is_empty()) {
        Draw_dimension_labels(buf_dc, buf_bmp, w, h, sel, in.monitor_rects_client,
                              in.paint_buffer, in.resources,
                              in.show_selection_size_side_labels,
                              in.show_selection_size_center_label, false);
    }
    if (!in.transient_center_label_text.empty() && !in.final_selection.Is_empty()) {
        Draw_transient_center_label(buf_dc, buf_bmp, w, h, in.final_selection,
                                    in.paint_buffer, in.resources,
                                    in.transient_center_label_text);
    }

    bool const show_crosshair = in.final_selection.Is_empty() && !in.dragging &&
                                !in.handle_dragging && !in.move_dragging &&
                                !in.modifier_preview;
    int const cx = in.cursor_client_px.x;
    int const cy = in.cursor_client_px.y;
    if (show_crosshair) {
        Draw_crosshair_and_coord_tooltip(buf_dc, buf_bmp, hwnd, w, h, cx, cy,
                                         in.paint_buffer, in.resources, in.capture);
    }

    if (in.highlight_handle.has_value() && in.resources && in.resources->handle_pen) {
        core::RectPx const &hl_sel = (in.handle_dragging || in.move_dragging)
                                         ? in.live_rect
                                         : in.final_selection;
        if (!hl_sel.Is_empty()) {
            Draw_border_highlight(buf_dc, in.resources->handle_pen, hl_sel,
                                  *in.highlight_handle, core::kMaxCornerSizePx);
        }
    }
    if (in.selected_annotation_bounds.has_value() && in.resources &&
        in.resources->handle_pen) {
        Draw_annotation_selection_corners(buf_dc, in.resources->handle_pen,
                                          *in.selected_annotation_bounds);
    }

    if (!in.toolbar_buttons.empty()) {
        ButtonDrawContext const btn_ctx{kCoordTooltipBg, kCoordTooltipText};
        for (IOverlayButton *const btn : in.toolbar_buttons) {
            if (btn) {
                btn->Draw(buf_dc, btn_ctx);
            }
        }
    }
    Draw_toolbar_tooltip(buf_dc, buf_bmp, w, h, in.paint_buffer, in.resources,
                         in.toolbar_tooltip_text, in.hovered_toolbar_bounds);

    BitBlt(hdc, 0, 0, w, h, buf_dc, 0, 0, SRCCOPY);
    SelectObject(buf_dc, old_buf);
    DeleteObject(buf_bmp);
    DeleteDC(buf_dc);
}

} // namespace greenflame
