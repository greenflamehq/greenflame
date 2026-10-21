#include "win/overlay_warning_dialog.h"

#include "greenflame_core/obfuscate_risk_warning.h"
#include "win/overlay_panel_chrome.h"
#include "win/ui_palette.h"

namespace {

constexpr int kWarningPanelWidthPx = 560;
constexpr int kWarningPanelHeightPx = 304;
constexpr int kWarningPanelMinWidthPx = 420;
constexpr int kWarningPanelMinHeightPx = 252;
constexpr int kWarningButtonHeightPx = 44;
constexpr int kWarningButtonGapPx = 16;
constexpr int kWarningSidePaddingPx = 44;
constexpr int kWarningBottomPaddingPx = 28;
constexpr int kWarningTitleHeightPx = 40;
constexpr int kWarningTitleTopPaddingPx = 24;
constexpr int kWarningBodyTopGapPx = 18;
constexpr int kWarningButtonTopGapPx = 20;
constexpr float kWarningTitleFontSizePt = 20.f;
constexpr float kWarningBodyFontSizePt = 13.f;
constexpr float kWarningButtonFontSizePt = 14.f;
constexpr D2D1_COLOR_F kWarningTitleColor = {
    237.f / greenflame::kOverlayPanelColorChannelMaxF,
    203.f / greenflame::kOverlayPanelColorChannelMaxF,
    24.f / greenflame::kOverlayPanelColorChannelMaxF, 1.f};
constexpr D2D1_COLOR_F kWarningBodyColor = {
    240.f / greenflame::kOverlayPanelColorChannelMaxF,
    240.f / greenflame::kOverlayPanelColorChannelMaxF,
    240.f / greenflame::kOverlayPanelColorChannelMaxF, 1.f};

} // namespace

namespace greenflame {

bool OverlayWarningDialog::Is_visible() const noexcept { return visible_; }

void OverlayWarningDialog::Hide() noexcept {
    visible_ = false;
    monitor_rect_client_ = std::nullopt;
    panel_rect_client_ = {};
    accept_button_.On_mouse_leave();
    reject_button_.On_mouse_leave();
}

void OverlayWarningDialog::Layout_buttons() noexcept {
    int32_t const content_width =
        panel_rect_client_.Width() - (kWarningSidePaddingPx * 2) - kWarningButtonGapPx;
    int32_t const button_width = content_width / 2;
    int32_t const button_top =
        panel_rect_client_.bottom - kWarningBottomPaddingPx - kWarningButtonHeightPx;
    int32_t const accept_left = panel_rect_client_.left + kWarningSidePaddingPx;
    int32_t const reject_left = accept_left + button_width + kWarningButtonGapPx;
    accept_button_.Set_position({accept_left, button_top});
    accept_button_.Set_size(button_width, kWarningButtonHeightPx);
    reject_button_.Set_position({reject_left, button_top});
    reject_button_.Set_size(button_width, kWarningButtonHeightPx);
}

void OverlayWarningDialog::Show_at_cursor(
    core::PointPx cursor_screen, std::span<const core::MonitorWithBounds> monitors,
    core::RectPx overlay_rect_screen) {
    visible_ = true;
    monitor_rect_client_ =
        Monitor_rect_in_client(cursor_screen, monitors, overlay_rect_screen);
    overlay_size_ = D2D1::SizeF(static_cast<float>(overlay_rect_screen.Width()),
                                static_cast<float>(overlay_rect_screen.Height()));
    core::RectPx const available_rect =
        monitor_rect_client_.value_or(core::RectPx::From_ltrb(
            0, 0, overlay_rect_screen.Width(), overlay_rect_screen.Height()));

    int32_t const max_panel_width = std::max(
        kWarningPanelMinWidthPx,
        available_rect.Width() - (static_cast<int32_t>(kOverlayPanelMarginPxF) * 2));
    int32_t const max_panel_height = std::max(
        kWarningPanelMinHeightPx,
        available_rect.Height() - (static_cast<int32_t>(kOverlayPanelMarginPxF) * 2));
    int32_t const panel_width = std::min(kWarningPanelWidthPx, max_panel_width);
    int32_t const panel_height = std::min(kWarningPanelHeightPx, max_panel_height);
    int32_t const panel_left =
        available_rect.left + std::max(0, (available_rect.Width() - panel_width) / 2);
    int32_t const panel_top =
        available_rect.top + std::max(0, (available_rect.Height() - panel_height) / 2);
    panel_rect_client_ = core::RectPx::From_ltrb(
        panel_left, panel_top, panel_left + panel_width, panel_top + panel_height);
    Layout_buttons();
}

bool OverlayWarningDialog::Update_hover(core::PointPx cursor_client) noexcept {
    bool const old_accept_hovered = accept_button_.Is_hovered();
    bool const old_reject_hovered = reject_button_.Is_hovered();

    if (accept_button_.Hit_test(cursor_client)) {
        accept_button_.On_mouse_enter();
        reject_button_.On_mouse_leave();
    } else if (reject_button_.Hit_test(cursor_client)) {
        accept_button_.On_mouse_leave();
        reject_button_.On_mouse_enter();
    } else {
        accept_button_.On_mouse_leave();
        reject_button_.On_mouse_leave();
    }

    return old_accept_hovered != accept_button_.Is_hovered() ||
           old_reject_hovered != reject_button_.Is_hovered();
}

void OverlayWarningDialog::On_mouse_down(core::PointPx cursor_client) noexcept {
    accept_button_.On_mouse_down(cursor_client);
    reject_button_.On_mouse_down(cursor_client);
}

OverlayWarningDialogAction
OverlayWarningDialog::On_mouse_up(core::PointPx cursor_client) noexcept {
    bool const accept_hit =
        accept_button_.Is_pressed() && accept_button_.Hit_test(cursor_client);
    bool const reject_hit =
        reject_button_.Is_pressed() && reject_button_.Hit_test(cursor_client);
    accept_button_.On_mouse_up(cursor_client);
    reject_button_.On_mouse_up(cursor_client);

    if (accept_hit) {
        return OverlayWarningDialogAction::Accept;
    }
    if (reject_hit) {
        return OverlayWarningDialogAction::Reject;
    }
    return OverlayWarningDialogAction::None;
}

bool OverlayWarningDialog::Ensure_dwrite_formats(IDWriteFactory *dwrite) noexcept {
    if (dwrite == nullptr) {
        return false;
    }
    if (dwrite_title_ && dwrite_body_ && dwrite_button_) {
        return true;
    }

    if (!dwrite_title_) {
        if (FAILED(dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, kWarningTitleFontSizePt, L"",
                dwrite_title_.ReleaseAndGetAddressOf()))) {
            return false;
        }
    }
    if (!dwrite_body_) {
        if (FAILED(dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                kWarningBodyFontSizePt, L"", dwrite_body_.ReleaseAndGetAddressOf()))) {
            return false;
        }
    }
    if (!dwrite_button_) {
        if (FAILED(dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, kWarningButtonFontSizePt, L"",
                dwrite_button_.ReleaseAndGetAddressOf()))) {
            return false;
        }
    }
    return true;
}

bool OverlayWarningDialog::Paint_d2d(ID2D1RenderTarget *rt, IDWriteFactory *dwrite,
                                     ID2D1SolidColorBrush *brush) noexcept {
    if (!visible_ || rt == nullptr || dwrite == nullptr || brush == nullptr ||
        panel_rect_client_.Is_empty()) {
        return false;
    }
    if (!Ensure_dwrite_formats(dwrite)) {
        return false;
    }

    D2D1_RECT_F const overlay_bounds =
        Overlay_panel_bounds(overlay_size_, monitor_rect_client_);
    D2D1_RECT_F const panel_bounds =
        D2D1::RectF(static_cast<float>(panel_rect_client_.left),
                    static_cast<float>(panel_rect_client_.top),
                    static_cast<float>(panel_rect_client_.right),
                    static_cast<float>(panel_rect_client_.bottom));
    Paint_overlay_panel_chrome(rt, brush, overlay_bounds, panel_bounds);

    auto draw_text = [&](IDWriteTextFormat *format, std::wstring_view text,
                         D2D1_RECT_F bounds, D2D1_COLOR_F color,
                         DWRITE_TEXT_ALIGNMENT alignment,
                         DWRITE_PARAGRAPH_ALIGNMENT paragraph_alignment =
                             DWRITE_PARAGRAPH_ALIGNMENT_NEAR) {
        if (format == nullptr || text.empty()) {
            return;
        }

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwrite->CreateTextLayout(
                text.data(), static_cast<UINT32>(text.size()), format,
                bounds.right - bounds.left, bounds.bottom - bounds.top,
                layout.GetAddressOf()))) {
            return;
        }
        layout->SetTextAlignment(alignment);
        layout->SetParagraphAlignment(paragraph_alignment);
        brush->SetColor(color);
        rt->DrawTextLayout(D2D1::Point2F(bounds.left, bounds.top), layout.Get(), brush);
    };

    std::wstring const body_text = std::wstring(core::kObfuscateRiskWarningLead) +
                                   L"\n\n" +
                                   std::wstring(core::kObfuscateRiskWarningGuidance);

    float const content_left =
        static_cast<float>(panel_rect_client_.left + kWarningSidePaddingPx);
    float const content_right =
        static_cast<float>(panel_rect_client_.right - kWarningSidePaddingPx);
    float const title_top =
        static_cast<float>(panel_rect_client_.top + kWarningTitleTopPaddingPx);
    float const title_bottom = title_top + static_cast<float>(kWarningTitleHeightPx);
    draw_text(dwrite_title_.Get(), core::kObfuscateRiskWarningTitle,
              D2D1::RectF(content_left, title_top, content_right, title_bottom),
              kWarningTitleColor, DWRITE_TEXT_ALIGNMENT_CENTER);

    core::RectPx const accept_bounds = accept_button_.Bounds();
    float const body_top = title_bottom + static_cast<float>(kWarningBodyTopGapPx);
    float const body_bottom =
        static_cast<float>(accept_bounds.top - kWarningButtonTopGapPx);
    draw_text(dwrite_body_.Get(), body_text,
              D2D1::RectF(content_left, body_top, content_right, body_bottom),
              kWarningBodyColor, DWRITE_TEXT_ALIGNMENT_LEADING);

    ButtonDrawContext const button_context{};
    accept_button_.Draw_d2d(rt, brush, button_context);
    reject_button_.Draw_d2d(rt, brush, button_context);

    auto draw_button_label = [&](OverlayRectButton const &button,
                                 std::wstring_view text) {
        core::RectPx const bounds = button.Bounds();
        ButtonVisualColors const colors =
            Resolve_button_visual_colors(false, button.Is_pressed(), button_context);
        draw_text(dwrite_button_.Get(), text,
                  D2D1::RectF(static_cast<float>(bounds.left + 8),
                              static_cast<float>(bounds.top + 8),
                              static_cast<float>(bounds.right - 8),
                              static_cast<float>(bounds.bottom - 8)),
                  colors.content_color, DWRITE_TEXT_ALIGNMENT_CENTER,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };
    draw_button_label(accept_button_, core::kObfuscateRiskAcceptLabel);
    draw_button_label(reject_button_, core::kObfuscateRiskRejectLabel);
    return true;
}

} // namespace greenflame
