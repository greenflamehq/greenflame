#include "win/overlay_help_overlay.h"

#include "greenflame_core/pixel_ops.h"
#include "win/gdi_capture.h"
#include "win/ui_palette.h"

namespace {

constexpr int kHelpTitleFontHeight = 26;
constexpr int kHelpBodyFontHeight = 18;
constexpr int kHelpKeyFontHeight = 18;
constexpr int kHelpSectionFontHeight = 20;
constexpr unsigned char kOverlayBackdropAlpha = 170;
constexpr int kPanelTopOffsetPx = 200;
constexpr int kMinPanelWidthPx = 360;
constexpr int kMinPanelHeightPx = 220;
constexpr unsigned char kPanelFillAlpha = 224;
constexpr int kTitleRowHeightPx = 42;
constexpr int kCloseHintRowHeightPx = 40;
constexpr int kRowsTopPaddingPx = 14;
constexpr int kPanelBottomPaddingPx = 24;

void Draw_help_overlay_to_buffer(HDC buf_dc, HBITMAP buf_bmp, int w, int h,
                                 std::span<uint8_t> pixels, HFONT title_font,
                                 HFONT body_font, HFONT key_font, HFONT section_font,
                                 greenflame::core::OverlayHelpContent const *content,
                                 std::optional<greenflame::core::RectPx> monitor_rect) {
    if (w <= 0 || h <= 0 || content == nullptr || content->sections.empty()) {
        return;
    }
    if (title_font == nullptr || body_font == nullptr || key_font == nullptr ||
        section_font == nullptr) {
        return;
    }

    int const row_bytes = greenflame::Row_bytes32(w);
    size_t const pix_size = static_cast<size_t>(row_bytes) * static_cast<size_t>(h);
    if (pixels.size() < pix_size) {
        return;
    }
    BITMAPINFOHEADER bmi_help;
    greenflame::Fill_bmi32_top_down(bmi_help, w, h);
    if (GetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                  reinterpret_cast<BITMAPINFO *>(&bmi_help), DIB_RGB_COLORS) == 0) {
        return;
    }

    greenflame::core::RectPx overlay_rect =
        monitor_rect.value_or(greenflame::core::RectPx::From_ltrb(0, 0, w, h));
    std::optional<greenflame::core::RectPx> const clipped_overlay =
        greenflame::core::RectPx::Clip(overlay_rect,
                                       greenflame::core::RectPx::From_ltrb(0, 0, w, h));
    if (!clipped_overlay.has_value()) {
        return;
    }
    overlay_rect = *clipped_overlay;
    greenflame::core::Blend_rect_onto_pixels(pixels, w, h, row_bytes, overlay_rect,
                                             RGB(0, 0, 0), kOverlayBackdropAlpha);

    int const overlay_w = overlay_rect.Width();
    int const overlay_h = overlay_rect.Height();
    int const panel_w = (std::max)(1, overlay_w / 2);
    int const panel_h = (std::max)(1, overlay_h / 2);
    int const panel_left = overlay_rect.left + (overlay_w - panel_w) / 2;
    int panel_top = overlay_rect.top + kPanelTopOffsetPx;
    int const max_panel_top = overlay_rect.bottom - panel_h;
    if (panel_top > max_panel_top) {
        panel_top = max_panel_top;
    }
    if (panel_top < overlay_rect.top) {
        panel_top = overlay_rect.top;
    }
    greenflame::core::RectPx const panel_rect = greenflame::core::RectPx::From_ltrb(
        panel_left, panel_top, panel_left + panel_w, panel_top + panel_h);
    if (panel_rect.Width() < kMinPanelWidthPx ||
        panel_rect.Height() < kMinPanelHeightPx) {
        SetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
                  reinterpret_cast<BITMAPINFO *>(&bmi_help), DIB_RGB_COLORS);
        return;
    }
    greenflame::core::Blend_rect_onto_pixels(pixels, w, h, row_bytes, panel_rect,
                                             RGB(52, 52, 52), kPanelFillAlpha);
    SetDIBits(buf_dc, buf_bmp, 0, static_cast<UINT>(h), pixels.data(),
              reinterpret_cast<BITMAPINFO *>(&bmi_help), DIB_RGB_COLORS);

    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
    if (border_pen) {
        HGDIOBJ old_pen = SelectObject(buf_dc, border_pen);
        SelectObject(buf_dc, GetStockObject(NULL_BRUSH));
        Rectangle(buf_dc, panel_rect.left, panel_rect.top, panel_rect.right,
                  panel_rect.bottom);
        SelectObject(buf_dc, old_pen);
        DeleteObject(border_pen);
    }

    int const content_left = panel_rect.left + 32;
    int const content_right = panel_rect.right - 32;
    int const top = panel_rect.top + 20;
    int const content_width = content_right - content_left;
    std::wstring_view const title_text(content->title);
    std::wstring_view const close_hint_text(content->close_hint);

    SetBkMode(buf_dc, TRANSPARENT);
    HGDIOBJ old_font = SelectObject(buf_dc, title_font);
    SetTextColor(buf_dc, RGB(242, 242, 242));

    if (!title_text.empty()) {
        RECT title_rect = {content_left, top, content_right, top + kTitleRowHeightPx};
        DrawTextW(buf_dc, title_text.data(), static_cast<int>(title_text.size()),
                  &title_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(buf_dc, body_font);
    if (!close_hint_text.empty()) {
        SetTextColor(buf_dc, RGB(208, 208, 208));
        RECT close_hint_rect = {content_left, top, content_right,
                                top + kCloseHintRowHeightPx};
        DrawTextW(buf_dc, close_hint_text.data(),
                  static_cast<int>(close_hint_text.size()), &close_hint_rect,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    int const sep_y = top + 50;
    HPEN sep_pen = CreatePen(PS_SOLID, 1, RGB(94, 94, 94));
    if (sep_pen) {
        HGDIOBJ old_sep_pen = SelectObject(buf_dc, sep_pen);
        MoveToEx(buf_dc, content_left, sep_y, nullptr);
        LineTo(buf_dc, content_right, sep_y);
        SelectObject(buf_dc, old_sep_pen);
        DeleteObject(sep_pen);
    }

    int key_col_w = 0;
    {
        HGDIOBJ old_key_measure_font = SelectObject(buf_dc, key_font);
        for (greenflame::core::OverlayHelpSection const &section : content->sections) {
            for (greenflame::core::OverlayHelpEntry const &entry : section.entries) {
                if (entry.shortcut.empty()) {
                    continue;
                }
                SIZE row_size{};
                if (GetTextExtentPoint32W(buf_dc, entry.shortcut.c_str(),
                                          static_cast<int>(entry.shortcut.size()),
                                          &row_size) != 0 &&
                    row_size.cx > key_col_w) {
                    key_col_w = row_size.cx;
                }
            }
        }
        SelectObject(buf_dc, old_key_measure_font);
    }
    key_col_w += 10;
    int const max_key_col_w = content_width / 2;
    if (key_col_w > max_key_col_w) {
        key_col_w = max_key_col_w;
    }
    int const desc_left = content_left + key_col_w + 10;
    int const row_h = 34;
    int row_y = sep_y + kRowsTopPaddingPx;

    for (greenflame::core::OverlayHelpSection const &section : content->sections) {
        if (section.entries.empty()) {
            continue;
        }
        if (row_y + row_h > panel_rect.bottom - kPanelBottomPaddingPx) {
            break;
        }

        SetTextColor(buf_dc, greenflame::kBorderColor);
        RECT section_rect = {desc_left, row_y, content_right, row_y + row_h};
        SelectObject(buf_dc, section_font);
        std::wstring_view const section_title(section.title);
        DrawTextW(buf_dc, section_title.data(), static_cast<int>(section_title.size()),
                  &section_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        row_y += row_h + 2;

        for (greenflame::core::OverlayHelpEntry const &entry : section.entries) {
            if (row_y + row_h > panel_rect.bottom - kPanelBottomPaddingPx) {
                break;
            }
            if (entry.shortcut.empty()) {
                continue;
            }
            SetTextColor(buf_dc, RGB(244, 220, 111));
            RECT key_rect = {content_left, row_y, content_left + key_col_w,
                             row_y + row_h};
            SelectObject(buf_dc, key_font);
            std::wstring_view const shortcut(entry.shortcut);
            DrawTextW(buf_dc, shortcut.data(), static_cast<int>(shortcut.size()),
                      &key_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SetTextColor(buf_dc, RGB(240, 240, 240));
            RECT desc_rect = {desc_left, row_y, content_right, row_y + row_h};
            SelectObject(buf_dc, body_font);
            std::wstring_view const description(entry.description);
            DrawTextW(buf_dc, description.data(), static_cast<int>(description.size()),
                      &desc_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            row_y += row_h;
        }
        row_y += 8;
        if (row_y + row_h > panel_rect.bottom - kPanelBottomPaddingPx) {
            break;
        }
    }

    SelectObject(buf_dc, old_font);
}

} // namespace

namespace greenflame {

OverlayHelpOverlay::OverlayHelpOverlay(core::OverlayHelpContent const *content)
    : content_(content) {}

OverlayHelpOverlay::~OverlayHelpOverlay() { Reset_fonts(); }

void OverlayHelpOverlay::Set_content(core::OverlayHelpContent const *content) noexcept {
    content_ = content;
    if (!Has_content()) {
        Hide();
    }
}

bool OverlayHelpOverlay::Has_content() const noexcept {
    if (content_ == nullptr) {
        return false;
    }
    for (core::OverlayHelpSection const &section : content_->sections) {
        if (!section.entries.empty()) {
            return true;
        }
    }
    return false;
}

bool OverlayHelpOverlay::Is_visible() const noexcept {
    return visible_ && Has_content();
}

void OverlayHelpOverlay::Hide() noexcept {
    visible_ = false;
    monitor_rect_client_ = std::nullopt;
}

void OverlayHelpOverlay::Hide_if_selection_unstable(bool selection_stable) noexcept {
    if (!selection_stable) {
        Hide();
    }
}

void OverlayHelpOverlay::Toggle_at_cursor(
    core::PointPx cursor_screen, std::span<const core::MonitorWithBounds> monitors,
    core::RectPx overlay_rect_screen) {
    if (!Has_content()) {
        Hide();
        return;
    }
    if (visible_) {
        Hide();
        return;
    }

    monitor_rect_client_ = std::nullopt;
    std::optional<size_t> const monitor_index =
        core::Index_of_monitor_containing(cursor_screen, monitors);
    if (monitor_index.has_value() && *monitor_index < monitors.size()) {
        core::RectPx const &monitor_bounds = monitors[*monitor_index].bounds;
        monitor_rect_client_ =
            core::RectPx::From_ltrb(monitor_bounds.left - overlay_rect_screen.left,
                                    monitor_bounds.top - overlay_rect_screen.top,
                                    monitor_bounds.right - overlay_rect_screen.left,
                                    monitor_bounds.bottom - overlay_rect_screen.top);
    }
    visible_ = true;
}

bool OverlayHelpOverlay::Paint(HDC hdc, RECT const &client_rect,
                               std::span<uint8_t> pixels) noexcept {
    if (!Is_visible() || hdc == nullptr) {
        return false;
    }
    if (!Ensure_fonts()) {
        return false;
    }
    int const w = client_rect.right - client_rect.left;
    int const h = client_rect.bottom - client_rect.top;
    if (w <= 0 || h <= 0) {
        return false;
    }

    HDC buf_dc = CreateCompatibleDC(hdc);
    HBITMAP buf_bmp = CreateCompatibleBitmap(hdc, w, h);
    if (!buf_dc || !buf_bmp) {
        if (buf_bmp) {
            DeleteObject(buf_bmp);
        }
        if (buf_dc) {
            DeleteDC(buf_dc);
        }
        return false;
    }

    HGDIOBJ old_buf = SelectObject(buf_dc, buf_bmp);
    BitBlt(buf_dc, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    Draw_help_overlay_to_buffer(buf_dc, buf_bmp, w, h, pixels, font_title_, font_body_,
                                font_key_, font_section_, content_,
                                monitor_rect_client_);
    BitBlt(hdc, 0, 0, w, h, buf_dc, 0, 0, SRCCOPY);
    SelectObject(buf_dc, old_buf);
    DeleteObject(buf_bmp);
    DeleteDC(buf_dc);
    return true;
}

bool OverlayHelpOverlay::Ensure_fonts() noexcept {
    if (font_title_ != nullptr && font_body_ != nullptr && font_key_ != nullptr &&
        font_section_ != nullptr) {
        return true;
    }

    if (font_title_ == nullptr) {
        font_title_ =
            CreateFontW(kHelpTitleFontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, FF_DONTCARE, L"Segoe UI");
    }
    if (font_body_ == nullptr) {
        font_body_ =
            CreateFontW(kHelpBodyFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, FF_DONTCARE, L"Segoe UI");
    }
    if (font_key_ == nullptr) {
        font_key_ =
            CreateFontW(kHelpKeyFontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
    }
    if (font_section_ == nullptr) {
        font_section_ =
            CreateFontW(kHelpSectionFontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, FF_DONTCARE, L"Segoe UI");
    }

    if (font_title_ == nullptr || font_body_ == nullptr || font_key_ == nullptr ||
        font_section_ == nullptr) {
        Reset_fonts();
        return false;
    }
    return true;
}

void OverlayHelpOverlay::Reset_fonts() noexcept {
    if (font_title_ != nullptr) {
        DeleteObject(font_title_);
        font_title_ = nullptr;
    }
    if (font_body_ != nullptr) {
        DeleteObject(font_body_);
        font_body_ = nullptr;
    }
    if (font_key_ != nullptr) {
        DeleteObject(font_key_);
        font_key_ = nullptr;
    }
    if (font_section_ != nullptr) {
        DeleteObject(font_section_);
        font_section_ = nullptr;
    }
}

} // namespace greenflame
