#include "win/overlay_help_overlay.h"
#include "win/ui_palette.h"

namespace {

constexpr unsigned char kOverlayBackdropAlpha = 170;
constexpr int kPanelTopOffsetPx = 200;
constexpr int kMinPanelWidthPx = 360;
constexpr int kMinPanelHeightPx = 220;
constexpr unsigned char kPanelFillAlpha = 224;
constexpr int kTitleRowHeightPx = 42;
constexpr int kCloseHintRowHeightPx = 40;
constexpr int kRowsTopPaddingPx = 14;
constexpr int kPanelBottomPaddingPx = 24;
constexpr float kColorChannelMaxF = 255.f;
constexpr float kHelpTitleFontSizePt = 17.f;
constexpr float kHelpBodyFontSizePt = 13.f;
constexpr float kHelpKeyFontSizePt = 13.f;
constexpr float kHelpSectionFontSizePt = 15.f;
constexpr float kHelpPanelSidePaddingPxF = 32.f;
constexpr float kHelpTitleTopPaddingPxF = 20.f;
constexpr float kHelpSeparatorOffsetPxF = 50.f;
constexpr float kHelpKeyColumnGapPxF = 10.f;
constexpr float kHelpRowHeightPxF = 34.f;
constexpr float kHelpSectionGapPxF = 8.f;
constexpr float kHelpTextMeasureExtentPxF = 8192.f;
constexpr float kHelpPanelBorderInsetPxF = 0.5f;
constexpr float kBackdropAlphaF =
    static_cast<float>(kOverlayBackdropAlpha) / kColorChannelMaxF;
constexpr float kPanelFillAlphaF =
    static_cast<float>(kPanelFillAlpha) / kColorChannelMaxF;
constexpr D2D1_COLOR_F kHelpBackdropColor = {0.f, 0.f, 0.f, kBackdropAlphaF};
constexpr D2D1_COLOR_F kHelpPanelFillColor = {
    52.f / kColorChannelMaxF, 52.f / kColorChannelMaxF, 52.f / kColorChannelMaxF,
    kPanelFillAlphaF};
constexpr D2D1_COLOR_F kHelpPanelBorderColor = {120.f / kColorChannelMaxF,
                                                120.f / kColorChannelMaxF,
                                                120.f / kColorChannelMaxF, 1.f};
constexpr D2D1_COLOR_F kHelpTitleColor = {242.f / kColorChannelMaxF,
                                          242.f / kColorChannelMaxF,
                                          242.f / kColorChannelMaxF, 1.f};
constexpr D2D1_COLOR_F kHelpCloseHintColor = {208.f / kColorChannelMaxF,
                                              208.f / kColorChannelMaxF,
                                              208.f / kColorChannelMaxF, 1.f};
constexpr D2D1_COLOR_F kHelpSeparatorColor = {
    94.f / kColorChannelMaxF, 94.f / kColorChannelMaxF, 94.f / kColorChannelMaxF, 1.f};
constexpr D2D1_COLOR_F kHelpShortcutColor = {244.f / kColorChannelMaxF,
                                             220.f / kColorChannelMaxF,
                                             111.f / kColorChannelMaxF, 1.f};
constexpr D2D1_COLOR_F kHelpBodyColor = {240.f / kColorChannelMaxF,
                                         240.f / kColorChannelMaxF,
                                         240.f / kColorChannelMaxF, 1.f};

} // namespace

namespace greenflame {

OverlayHelpOverlay::OverlayHelpOverlay(core::OverlayHelpContent const *content)
    : content_(content) {}

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

void OverlayHelpOverlay::Show_at_cursor(
    core::PointPx cursor_screen, std::span<const core::MonitorWithBounds> monitors,
    core::RectPx overlay_rect_screen) {
    if (!Has_content()) {
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

void OverlayHelpOverlay::Toggle_at_cursor(
    core::PointPx cursor_screen, std::span<const core::MonitorWithBounds> monitors,
    core::RectPx overlay_rect_screen) {
    if (visible_) {
        Hide();
        return;
    }
    Show_at_cursor(cursor_screen, monitors, overlay_rect_screen);
}

bool OverlayHelpOverlay::Ensure_dwrite_formats(IDWriteFactory *dwrite) noexcept {
    if (!dwrite) {
        return false;
    }
    if (dwrite_title_ && dwrite_body_ && dwrite_key_ && dwrite_section_) {
        return true;
    }

    // Match the established overlay text sizing at 96 DPI.
    if (!dwrite_title_) {
        if (FAILED(dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, kHelpTitleFontSizePt, L"",
                dwrite_title_.ReleaseAndGetAddressOf()))) {
            return false;
        }
    }
    if (!dwrite_body_) {
        if (FAILED(dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                kHelpBodyFontSizePt, L"", dwrite_body_.ReleaseAndGetAddressOf()))) {
            return false;
        }
    }
    if (!dwrite_key_) {
        if (FAILED(dwrite->CreateTextFormat(
                L"Courier New", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                kHelpKeyFontSizePt, L"", dwrite_key_.ReleaseAndGetAddressOf()))) {
            return false;
        }
    }
    if (!dwrite_section_) {
        if (FAILED(dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, kHelpSectionFontSizePt, L"",
                dwrite_section_.ReleaseAndGetAddressOf()))) {
            return false;
        }
    }
    return true;
}

bool OverlayHelpOverlay::Paint_d2d(ID2D1RenderTarget *rt, IDWriteFactory *dwrite,
                                   ID2D1SolidColorBrush *brush) noexcept {
    if (!Is_visible() || !rt || !dwrite || !brush || !content_ ||
        content_->sections.empty()) {
        return false;
    }
    if (!Ensure_dwrite_formats(dwrite)) {
        return false;
    }

    D2D1_SIZE_F const rt_size = rt->GetSize();
    float const w = rt_size.width;
    float const h = rt_size.height;
    if (w <= 0.f || h <= 0.f) {
        return false;
    }

    float ov_l = 0.f;
    float ov_t = 0.f;
    float ov_r = w;
    float ov_b = h;
    if (monitor_rect_client_.has_value() && !monitor_rect_client_->Is_empty()) {
        ov_l = static_cast<float>(monitor_rect_client_->left);
        ov_t = static_cast<float>(monitor_rect_client_->top);
        ov_r = static_cast<float>(monitor_rect_client_->right);
        ov_b = static_cast<float>(monitor_rect_client_->bottom);
    }

    brush->SetColor(kHelpBackdropColor);
    rt->FillRectangle(D2D1::RectF(ov_l, ov_t, ov_r, ov_b), brush);

    float const ov_w = ov_r - ov_l;
    float const ov_h = ov_b - ov_t;
    float const panel_w = std::max(1.f, ov_w / 2.f);
    float const panel_h = std::max(1.f, ov_h / 2.f);
    float const panel_l = ov_l + (ov_w - panel_w) / 2.f;
    float panel_t = ov_t + static_cast<float>(kPanelTopOffsetPx);
    float const max_panel_t = ov_b - panel_h;
    if (panel_t > max_panel_t) {
        panel_t = max_panel_t;
    }
    if (panel_t < ov_t) {
        panel_t = ov_t;
    }
    float const panel_r = panel_l + panel_w;
    float const panel_b = panel_t + panel_h;

    if (panel_w < static_cast<float>(kMinPanelWidthPx) ||
        panel_h < static_cast<float>(kMinPanelHeightPx)) {
        return true;
    }

    brush->SetColor(kHelpPanelFillColor);
    rt->FillRectangle(D2D1::RectF(panel_l, panel_t, panel_r, panel_b), brush);

    brush->SetColor(kHelpPanelBorderColor);
    rt->DrawRectangle(D2D1::RectF(panel_l + kHelpPanelBorderInsetPxF,
                                  panel_t + kHelpPanelBorderInsetPxF,
                                  panel_r - kHelpPanelBorderInsetPxF,
                                  panel_b - kHelpPanelBorderInsetPxF),
                      brush, 1.f);

    float const content_l = panel_l + kHelpPanelSidePaddingPxF;
    float const content_r = panel_r - kHelpPanelSidePaddingPxF;
    float const content_w = content_r - content_l;
    float const top_row = panel_t + kHelpTitleTopPaddingPxF;

    auto draw_text = [&](IDWriteTextFormat *fmt, std::wstring_view text, float x,
                         float y, float max_width, float max_height, D2D1_COLOR_F color,
                         DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
        if (!fmt || text.empty()) {
            return;
        }
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwrite->CreateTextLayout(
                text.data(), static_cast<UINT32>(text.size()), fmt, max_width,
                max_height, layout.GetAddressOf()))) {
            return;
        }
        layout->SetTextAlignment(align);
        brush->SetColor(color);
        rt->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush);
    };

    if (!content_->title.empty()) {
        draw_text(dwrite_title_.Get(), content_->title, content_l, top_row, content_w,
                  static_cast<float>(kTitleRowHeightPx), kHelpTitleColor);
    }
    if (!content_->close_hint.empty()) {
        draw_text(dwrite_body_.Get(), content_->close_hint, content_l, top_row,
                  content_w, static_cast<float>(kCloseHintRowHeightPx),
                  kHelpCloseHintColor, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    float const sep_y = top_row + kHelpSeparatorOffsetPxF;
    brush->SetColor(kHelpSeparatorColor);
    rt->DrawLine(D2D1::Point2F(content_l, sep_y), D2D1::Point2F(content_r, sep_y),
                 brush, 1.f);

    float key_col_w = 0.f;
    for (core::OverlayHelpSection const &section : content_->sections) {
        for (core::OverlayHelpEntry const &entry : section.entries) {
            if (entry.shortcut.empty()) {
                continue;
            }
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            if (FAILED(dwrite->CreateTextLayout(
                    entry.shortcut.c_str(), static_cast<UINT32>(entry.shortcut.size()),
                    dwrite_key_.Get(), kHelpTextMeasureExtentPxF,
                    kHelpTextMeasureExtentPxF, layout.GetAddressOf()))) {
                continue;
            }
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(layout->GetMetrics(&metrics)) && metrics.width > key_col_w) {
                key_col_w = metrics.width;
            }
        }
    }
    key_col_w += kHelpKeyColumnGapPxF;
    float const max_key_col_w = content_w / 2.f;
    if (key_col_w > max_key_col_w) {
        key_col_w = max_key_col_w;
    }
    float const desc_l = content_l + key_col_w + kHelpKeyColumnGapPxF;
    float row_y = sep_y + static_cast<float>(kRowsTopPaddingPx);

    for (core::OverlayHelpSection const &section : content_->sections) {
        if (section.entries.empty()) {
            continue;
        }
        if (row_y + kHelpRowHeightPxF >
            panel_b - static_cast<float>(kPanelBottomPaddingPx)) {
            break;
        }

        draw_text(dwrite_section_.Get(), section.title, desc_l, row_y,
                  content_r - desc_l, kHelpRowHeightPxF, kBorderColor);
        row_y += kHelpRowHeightPxF + 2.f;

        for (core::OverlayHelpEntry const &entry : section.entries) {
            if (row_y + kHelpRowHeightPxF >
                panel_b - static_cast<float>(kPanelBottomPaddingPx)) {
                break;
            }
            if (entry.shortcut.empty()) {
                continue;
            }
            draw_text(dwrite_key_.Get(), entry.shortcut, content_l, row_y, key_col_w,
                      kHelpRowHeightPxF, kHelpShortcutColor);
            draw_text(dwrite_body_.Get(), entry.description, desc_l, row_y,
                      content_r - desc_l, kHelpRowHeightPxF, kHelpBodyColor);
            row_y += kHelpRowHeightPxF;
        }
        row_y += kHelpSectionGapPxF;
    }

    return true;
}

} // namespace greenflame
