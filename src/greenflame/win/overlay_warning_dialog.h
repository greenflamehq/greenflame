#pragma once

#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"
#include "win/overlay_button.h"
#include "win/overlay_top_layer.h"

namespace greenflame {

enum class OverlayWarningDialogAction : uint8_t {
    None = 0,
    Accept = 1,
    Reject = 2,
};

class OverlayWarningDialog final : public IOverlayTopLayer {
  public:
    OverlayWarningDialog() = default;
    ~OverlayWarningDialog() override = default;

    OverlayWarningDialog(OverlayWarningDialog const &) = delete;
    OverlayWarningDialog &operator=(OverlayWarningDialog const &) = delete;
    OverlayWarningDialog(OverlayWarningDialog &&) = delete;
    OverlayWarningDialog &operator=(OverlayWarningDialog &&) = delete;

    [[nodiscard]] bool Is_visible() const noexcept override;
    void Hide() noexcept;
    void Show_at_cursor(core::PointPx cursor_screen,
                        std::span<const core::MonitorWithBounds> monitors,
                        core::RectPx overlay_rect_screen);
    [[nodiscard]] bool Update_hover(core::PointPx cursor_client) noexcept;
    void On_mouse_down(core::PointPx cursor_client) noexcept;
    [[nodiscard]] OverlayWarningDialogAction
    On_mouse_up(core::PointPx cursor_client) noexcept;
    [[nodiscard]] bool Paint_d2d(ID2D1RenderTarget *rt, IDWriteFactory *dwrite,
                                 ID2D1SolidColorBrush *brush) noexcept override;

  private:
    [[nodiscard]] bool Ensure_dwrite_formats(IDWriteFactory *dwrite) noexcept;
    void Layout_buttons() noexcept;

    bool visible_ = false;
    std::optional<core::RectPx> monitor_rect_client_ = std::nullopt;
    // The whole overlay's size (the virtual desktop), set by Show_at_cursor. A paint
    // target may cover only part of the overlay, so its size is not this.
    D2D1_SIZE_F overlay_size_ = {};
    core::RectPx panel_rect_client_ = {};
    OverlayRectButton accept_button_ = {};
    OverlayRectButton reject_button_ = {};
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_title_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_body_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_button_;
};

} // namespace greenflame
