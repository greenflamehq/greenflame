// Overlay painting: capture blit, selection dim/border, dimension labels,
// crosshair, round magnifier and coord tooltip. Implementation and internal
// helpers.

#include "overlay_paint.h"

#include "gdi_capture.h"
#include "greenflame_core/pixel_ops.h"
#include "greenflame_core/rect_px.h"

namespace {

constexpr unsigned char kCoordTooltipAlpha = 200;
constexpr unsigned char kCoordTooltipBgR = 217, kCoordTooltipBgG = 240,
                        kCoordTooltipBgB = 227;
constexpr COLORREF kCoordTooltipBorderText = RGB(15, 60, 35); // dark SeaGreen

constexpr int kHandleHalfSize = 4; // 8x8 handle centered on contour
constexpr int kMagnifierSize = 256;
constexpr int kMagnifierZoom = 8; // source size = kMagnifierSize / kMagnifierZoom
constexpr int kMagnifierSource = kMagnifierSize / kMagnifierZoom; // 32
constexpr int kMagnifierPadding = 8;
// Magnifier crosshair and contour: 66% opaque.
constexpr unsigned char kMagnifierCrosshairAlpha = 168; // 255 * 66 / 100
// Greenshot-style: thick cross with white contour, center gap, margin from edge.
constexpr int kMagnifierCrosshairThickness = 8;
constexpr int kMagnifierCrosshairGap = 20;   // center to inner arm end
constexpr int kMagnifierCrosshairMargin = 8; // circle edge to outer arm end
constexpr int kMagnifierCheckerTile = 16;
constexpr COLORREF kMagnifierCheckerLight = RGB(224, 224, 224);
constexpr COLORREF kMagnifierCheckerDark = RGB(168, 168, 168);
constexpr int kColorChannelMax = 255;
constexpr float kColorChannelMaxF = static_cast<float>(kColorChannelMax);

void Blend_magnifier_crosshair_onto_pixels(std::span<uint8_t> pixels, int width,
                                           int height, int row_bytes, int mag_left,
                                           int mag_top) noexcept {
    int const size = kMagnifierSize;
    int const center = size / 2;
    int const radius_sq = center * center;
    int const half = kMagnifierCrosshairThickness / 2;
    int const gap = kMagnifierCrosshairGap;
    int const margin = kMagnifierCrosshairMargin;
    float const a = static_cast<float>(kMagnifierCrosshairAlpha) / 255.f;
    float const ia = 1.f - a;

    // Four arm rectangles in magnifier-local coords [x0,x1) x [y0,y1).
    struct Arm {
        int x0, y0, x1, y1;
    };
    Arm const arms[4] = {
        {center - half, margin, center + half, center - gap},        // top
        {center - half, center + gap, center + half, size - margin}, // bottom
        {margin, center - half, center - gap, center + half},        // left
        {center + gap, center - half, size - margin, center + half}, // right
    };

    int const sy0 = std::max(0, mag_top);
    int const sy1 = std::min(height, mag_top + size);
    int const sx0 = std::max(0, mag_left);
    int const sx1 = std::min(width, mag_left + size);

    for (int y = sy0; y < sy1; ++y) {
        int const iy = y - mag_top;
        int const dy_sq = (iy - center) * (iy - center);
        size_t const row_offset =
            static_cast<size_t>(y) * static_cast<size_t>(row_bytes);
        for (int x = sx0; x < sx1; ++x) {
            int const ix = x - mag_left;
            if ((ix - center) * (ix - center) + dy_sq > radius_sq) continue;

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

void Fill_magnifier_checkerboard(HDC dc, int left, int top, int src_x,
                                 int src_y) noexcept {
    HBRUSH const dc_brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    if (!dc_brush) {
        RECT fallback = {left, top, left + kMagnifierSize, top + kMagnifierSize};
        FillRect(dc, &fallback, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return;
    }

    // Offset the checkerboard phase by the source coordinates so the pattern
    // scrolls with the mouse, giving visual motion feedback near edges.
    int const phase_x = ((src_x % 2) + 2) % 2;
    int const phase_y = ((src_y % 2) + 2) % 2;

    for (int y = 0; y < kMagnifierSize; y += kMagnifierCheckerTile) {
        int const cell_top = top + y;
        int const cell_bottom =
            std::min(top + y + kMagnifierCheckerTile, top + kMagnifierSize);
        for (int x = 0; x < kMagnifierSize; x += kMagnifierCheckerTile) {
            int const cell_left = left + x;
            int const cell_right =
                std::min(left + x + kMagnifierCheckerTile, left + kMagnifierSize);
            bool const dark = (((x / kMagnifierCheckerTile) + phase_x +
                                (y / kMagnifierCheckerTile) + phase_y) &
                               1) != 0;
            SetDCBrushColor(dc, dark ? kMagnifierCheckerDark : kMagnifierCheckerLight);
            RECT cell = {cell_left, cell_top, cell_right, cell_bottom};
            FillRect(dc, &cell, dc_brush);
        }
    }
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

void Draw_contour_handles(HDC buf_dc, greenflame::core::RectPx const &sel,
                          greenflame::PaintResources const *res) {
    if (sel.Is_empty()) return;
    greenflame::core::RectPx const r = sel.Normalized();
    int const cx = (r.left + r.right) / 2;
    int const cy = (r.top + r.bottom) / 2;
    if (!res || !res->handle_brush || !res->handle_pen) return;
    HGDIOBJ old_brush = SelectObject(buf_dc, res->handle_brush);
    HGDIOBJ old_pen = SelectObject(buf_dc, res->handle_pen);
    auto draw_handle = [buf_dc](int hx, int hy) {
        RECT hr = {hx - kHandleHalfSize, hy - kHandleHalfSize, hx + kHandleHalfSize,
                   hy + kHandleHalfSize};
        Rectangle(buf_dc, hr.left, hr.top, hr.right, hr.bottom);
    };
    draw_handle(r.left, r.top);
    draw_handle(r.right, r.top);
    draw_handle(r.right, r.bottom);
    draw_handle(r.left, r.bottom);
    draw_handle(cx, r.top);
    draw_handle(r.right, cy);
    draw_handle(cx, r.bottom);
    draw_handle(r.left, cy);
    SelectObject(buf_dc, old_pen);
    SelectObject(buf_dc, old_brush);
}

void Draw_selection_dim_and_border(HDC buf_dc, HBITMAP buf_bmp, int w, int h,
                                   greenflame::core::RectPx const &sel,
                                   std::span<uint8_t> pixels,
                                   greenflame::PaintResources const *res) {
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
    if (res && res->sel_border_brush) {
        RECT sel_rc = {sel.left, sel.top, sel.right, sel.bottom};
        FrameRect(buf_dc, &sel_rc, res->sel_border_brush);
    }
}

void Draw_dimension_labels(HDC buf_dc, HBITMAP buf_bmp, int w, int h,
                           greenflame::core::RectPx const &sel,
                           std::span<uint8_t> pixels,
                           greenflame::PaintResources const *res) {
    constexpr int k_dim_margin = 4;
    constexpr int k_dim_gap = 4;
    constexpr int k_center_margin_v = 5;
    int const row_bytes = greenflame::Row_bytes32(w);
    size_t const pix_size = static_cast<size_t>(row_bytes) * static_cast<size_t>(h);
    if (pixels.size() < pix_size) return;
    BITMAPINFOHEADER bmi_dim;
    greenflame::Fill_bmi32_top_down(bmi_dim, w, h);
    if (GetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                  reinterpret_cast<BITMAPINFO *>(&bmi_dim), DIB_RGB_COLORS) != 0) {
        HFONT font_dim = res ? res->font_dim : nullptr;
        HFONT font_center = res ? res->font_center : nullptr;
        if (font_dim) {
            HGDIOBJ old_font_dim = SelectObject(buf_dc, font_dim);

            std::wstring width_str = std::to_wstring(sel.Width());
            std::wstring height_str = std::to_wstring(sel.Height());
            std::wstring center_str =
                std::to_wstring(sel.Width()) + L" x " + std::to_wstring(sel.Height());

            SIZE width_size = {}, height_size = {}, center_size = {};
            GetTextExtentPoint32W(buf_dc, width_str.c_str(),
                                  static_cast<int>(width_str.size()), &width_size);
            GetTextExtentPoint32W(buf_dc, height_str.c_str(),
                                  static_cast<int>(height_str.size()), &height_size);
            if (font_center) {
                SelectObject(buf_dc, font_center);
                GetTextExtentPoint32W(buf_dc, center_str.c_str(),
                                      static_cast<int>(center_str.size()),
                                      &center_size);
                SelectObject(buf_dc, font_dim);
            }

            int const width_box_w = width_size.cx + 2 * k_dim_margin;
            int const width_box_h = width_size.cy + 2 * k_dim_margin;
            int const height_box_w = height_size.cx + 2 * k_dim_margin;
            int const height_box_h = height_size.cy + 2 * k_dim_margin;
            int const center_box_w = center_size.cx + 2 * k_dim_margin;
            int const center_box_h = center_size.cy + 2 * k_center_margin_v;

            int const center_x = (sel.left + sel.right) / 2;
            int const center_y = (sel.top + sel.bottom) / 2;

            int width_box_left = center_x - width_box_w / 2;
            int width_box_top;
            bool above_fits = (sel.top - width_box_h - k_dim_gap >= 0);
            bool below_fits = (sel.bottom + k_dim_gap + width_box_h <= h);
            if (above_fits) {
                width_box_top = sel.top - width_box_h - k_dim_gap;
            } else if (below_fits) {
                width_box_top = sel.bottom + k_dim_gap;
            } else {
                width_box_top = (sel.top - width_box_h - k_dim_gap >= h - width_box_h)
                                    ? (sel.bottom + k_dim_gap)
                                    : (sel.top - width_box_h - k_dim_gap);
            }
            if (width_box_top < 0) width_box_top = 0;
            if (width_box_top + width_box_h > h) width_box_top = h - width_box_h;

            int height_box_top = center_y - height_box_h / 2;
            int height_box_left;
            int height_box_right_left = sel.left - k_dim_gap - height_box_w;
            int height_box_right_right = sel.right + k_dim_gap;
            bool left_fits = (height_box_right_left >= 0);
            bool right_fits = (height_box_right_right + height_box_w <= w);
            if (left_fits) {
                height_box_left = height_box_right_left;
            } else if (right_fits) {
                height_box_left = height_box_right_right;
            } else {
                height_box_left = (height_box_right_left >= w - height_box_w)
                                      ? height_box_right_right
                                      : height_box_right_left;
            }
            if (height_box_left < 0) height_box_left = 0;
            if (height_box_left + height_box_w > w) height_box_left = w - height_box_w;

            int center_box_left = center_x - center_box_w / 2;
            int center_box_top = center_y - center_box_h / 2;
            constexpr int k_center_min_padding = 24;
            int const sel_w = sel.Width();
            int const sel_h = sel.Height();
            bool const center_fits =
                (sel_w >= center_box_w + 2 * k_center_min_padding &&
                 sel_h >= center_box_h + 2 * k_center_min_padding);

            greenflame::core::RectPx width_box_rect =
                greenflame::core::RectPx::From_ltrb(width_box_left, width_box_top,
                                                    width_box_left + width_box_w,
                                                    width_box_top + width_box_h);
            greenflame::core::RectPx height_box_rect =
                greenflame::core::RectPx::From_ltrb(height_box_left, height_box_top,
                                                    height_box_left + height_box_w,
                                                    height_box_top + height_box_h);
            greenflame::core::RectPx center_box_rect =
                greenflame::core::RectPx::From_ltrb(center_box_left, center_box_top,
                                                    center_box_left + center_box_w,
                                                    center_box_top + center_box_h);

            constexpr unsigned char side_box_alpha = 150;
            constexpr unsigned char center_box_alpha = kCoordTooltipAlpha / 2;
            greenflame::core::Blend_rect_onto_pixels(
                pixels, w, h, row_bytes, width_box_rect, kCoordTooltipBgR,
                kCoordTooltipBgG, kCoordTooltipBgB, side_box_alpha);
            greenflame::core::Blend_rect_onto_pixels(
                pixels, w, h, row_bytes, height_box_rect, kCoordTooltipBgR,
                kCoordTooltipBgG, kCoordTooltipBgB, side_box_alpha);
            if (center_fits) {
                greenflame::core::Blend_rect_onto_pixels(
                    pixels, w, h, row_bytes, center_box_rect, kCoordTooltipBgR,
                    kCoordTooltipBgG, kCoordTooltipBgB, center_box_alpha);
            }
            SetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                      reinterpret_cast<BITMAPINFO *>(&bmi_dim), DIB_RGB_COLORS);

            HPEN dark_pen = CreatePen(PS_SOLID, 1, kCoordTooltipBorderText);
            if (dark_pen) {
                HGDIOBJ old_dim_pen = SelectObject(buf_dc, dark_pen);
                SelectObject(buf_dc, GetStockObject(NULL_BRUSH));
                Rectangle(buf_dc, width_box_left, width_box_top,
                          width_box_left + width_box_w, width_box_top + width_box_h);
                Rectangle(buf_dc, height_box_left, height_box_top,
                          height_box_left + height_box_w,
                          height_box_top + height_box_h);
                if (center_fits) {
                    Rectangle(buf_dc, center_box_left, center_box_top,
                              center_box_left + center_box_w,
                              center_box_top + center_box_h);
                }
                SelectObject(buf_dc, old_dim_pen);
                DeleteObject(dark_pen);
            }
            SetBkMode(buf_dc, TRANSPARENT);
            SetTextColor(buf_dc, kCoordTooltipBorderText);
            RECT width_text_rc = {width_box_left + k_dim_margin,
                                  width_box_top + k_dim_margin,
                                  width_box_left + width_box_w - k_dim_margin,
                                  width_box_top + width_box_h - k_dim_margin};
            RECT height_text_rc = {height_box_left + k_dim_margin,
                                   height_box_top + k_dim_margin,
                                   height_box_left + height_box_w - k_dim_margin,
                                   height_box_top + height_box_h - k_dim_margin};
            RECT center_text_rc = {center_box_left + k_dim_margin,
                                   center_box_top + k_center_margin_v,
                                   center_box_left + center_box_w - k_dim_margin,
                                   center_box_top + center_box_h - k_center_margin_v};
            DrawTextW(buf_dc, width_str.c_str(), -1, &width_text_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextW(buf_dc, height_str.c_str(), -1, &height_text_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (center_fits && font_center) {
                SelectObject(buf_dc, font_center);
                DrawTextW(buf_dc, center_str.c_str(), -1, &center_text_rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(buf_dc, old_font_dim);
        }
    }
}

void Draw_crosshair_and_coord_tooltip(HDC buf_dc, HBITMAP buf_bmp, HWND hwnd, int w,
                                      int h, int cx, int cy, bool dragging,
                                      std::span<uint8_t> pixels,
                                      greenflame::PaintResources const *res,
                                      greenflame::GdiCaptureResult const *capture) {
    if (cx < 0 || cx >= w || cy < 0 || cy >= h) return;

    if (!dragging) {
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
        int const src_x = cx - kMagnifierSource / 2;
        int const src_y = cy - kMagnifierSource / 2;
        int const src_right = src_x + kMagnifierSource;
        int const src_bottom = src_y + kMagnifierSource;
        int const sample_left = std::max(src_x, mon_left);
        int const sample_top = std::max(src_y, mon_top);
        int const sample_right = std::min(src_right, mon_right);
        int const sample_bottom = std::min(src_bottom, mon_bottom);
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
            Fill_magnifier_checkerboard(buf_dc, mag_left, mag_top, src_x, src_y);

            if (source_has_coverage) {
                int const dst_left = mag_left + (sample_left - src_x) * kMagnifierZoom;
                int const dst_top = mag_top + (sample_top - src_y) * kMagnifierZoom;
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
                    StretchBlt(buf_dc, dst_left, dst_top, dst_w, dst_h, buf_dc,
                               sample_left, sample_top, sample_right - sample_left,
                               sample_bottom - sample_top, SRCCOPY);
                }
            }
            SelectClipRgn(buf_dc, nullptr);
            DeleteObject(rgn);
        }
        HPEN mag_border_pen = res ? res->border_pen : nullptr;
        if (mag_border_pen) {
            HGDIOBJ old_pen = SelectObject(buf_dc, mag_border_pen);
            SelectObject(buf_dc, GetStockObject(NULL_BRUSH));
            Ellipse(buf_dc, mag_left, mag_top, mag_left + kMagnifierSize,
                    mag_top + kMagnifierSize);
            SelectObject(buf_dc, old_pen);
        }

        // Compute coord tooltip position (no pixel buffer needed).
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

        // Single DIB round-trip for magnifier crosshair + tooltip background.
        int const row_bytes = greenflame::Row_bytes32(w);
        size_t const pix_size = static_cast<size_t>(row_bytes) * static_cast<size_t>(h);
        if (pixels.size() >= pix_size) {
            BITMAPINFOHEADER bmi;
            greenflame::Fill_bmi32_top_down(bmi, w, h);
            if (GetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                          reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS) != 0) {
                Blend_magnifier_crosshair_onto_pixels(pixels, w, h, row_bytes, mag_left,
                                                      mag_top);
                if (tooltip_ready) {
                    greenflame::core::Blend_rect_onto_pixels(
                        pixels, w, h, row_bytes, box_rect, kCoordTooltipBgR,
                        kCoordTooltipBgG, kCoordTooltipBgB, kCoordTooltipAlpha);
                }
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
            SetTextColor(buf_dc, kCoordTooltipBorderText);
            RECT text_rc = {tt_left + k_coord_margin, tt_top + k_coord_margin,
                            tt_left + box_w - k_coord_margin,
                            tt_top + box_h - k_coord_margin};
            DrawTextW(buf_dc, coord_str.c_str(), -1, &text_rc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        if (old_font) SelectObject(buf_dc, old_font);
    }
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
                                  in.resources);

    if ((in.dragging || in.handle_dragging || in.move_dragging ||
         in.modifier_preview) &&
        !sel.Is_empty()) {
        Draw_dimension_labels(buf_dc, buf_bmp, w, h, sel, in.paint_buffer,
                              in.resources);
    }

    bool const show_crosshair = in.final_selection.Is_empty() && !in.dragging &&
                                !in.handle_dragging && !in.move_dragging &&
                                !in.modifier_preview;
    int const cx = in.cursor_client_px.x;
    int const cy = in.cursor_client_px.y;
    if (show_crosshair) {
        Draw_crosshair_and_coord_tooltip(buf_dc, buf_bmp, hwnd, w, h, cx, cy, false,
                                         in.paint_buffer, in.resources, in.capture);
        // Resize handles only when committed or resizing; never in Object_selection
        // (modifier_preview).
    } else if ((in.handle_dragging || in.move_dragging) && !in.live_rect.Is_empty()) {
        Draw_contour_handles(buf_dc, in.live_rect, in.resources);
    } else if (!in.final_selection.Is_empty() && !in.modifier_preview) {
        Draw_contour_handles(buf_dc, in.final_selection, in.resources);
    }

    BitBlt(hdc, 0, 0, w, h, buf_dc, 0, 0, SRCCOPY);
    SelectObject(buf_dc, old_buf);
    DeleteObject(buf_bmp);
    DeleteDC(buf_dc);
}

} // namespace greenflame
