#pragma once

#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/overlay_help_content.h"
#include "win/overlay_top_layer.h"

namespace greenflame {

class OverlayHelpOverlay final : public IOverlayTopLayer {
  public:
    OverlayHelpOverlay() = default;
    explicit OverlayHelpOverlay(core::OverlayHelpContent const *content);
    ~OverlayHelpOverlay() override = default;

    OverlayHelpOverlay(OverlayHelpOverlay const &) = delete;
    OverlayHelpOverlay &operator=(OverlayHelpOverlay const &) = delete;
    OverlayHelpOverlay(OverlayHelpOverlay &&) = delete;
    OverlayHelpOverlay &operator=(OverlayHelpOverlay &&) = delete;

    void Set_content(core::OverlayHelpContent const *content) noexcept;
    [[nodiscard]] bool Has_content() const noexcept;
    [[nodiscard]] bool Is_visible() const noexcept override;
    void Hide() noexcept;
    void Hide_if_selection_unstable(bool selection_stable) noexcept;

    void Show_at_cursor(core::PointPx cursor_screen,
                        std::span<const core::MonitorWithBounds> monitors,
                        core::RectPx overlay_rect_screen);
    void Toggle_at_cursor(core::PointPx cursor_screen,
                          std::span<const core::MonitorWithBounds> monitors,
                          core::RectPx overlay_rect_screen);
    [[nodiscard]] bool Paint_d2d(ID2D1RenderTarget *rt, IDWriteFactory *dwrite,
                                 ID2D1SolidColorBrush *brush) noexcept override;

  private:
    [[nodiscard]] bool Ensure_dwrite_formats(IDWriteFactory *dwrite) noexcept;

    core::OverlayHelpContent const *content_ = nullptr;
    bool visible_ = false;
    std::optional<core::RectPx> monitor_rect_client_ = std::nullopt;
    // The whole overlay's size (the virtual desktop), set by Show_at_cursor. A paint
    // target may cover only part of the overlay, so its size is not this.
    D2D1_SIZE_F overlay_size_ = {};
    // DWrite text formats are created lazily and reused across overlay frames.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_title_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_body_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_key_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_section_;
};

} // namespace greenflame
