#pragma once

#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/overlay_help_content.h"

namespace greenflame {

class OverlayHelpOverlay final {
  public:
    OverlayHelpOverlay() = default;
    explicit OverlayHelpOverlay(core::OverlayHelpContent const *content);
    ~OverlayHelpOverlay() = default;

    OverlayHelpOverlay(OverlayHelpOverlay const &) = delete;
    OverlayHelpOverlay &operator=(OverlayHelpOverlay const &) = delete;
    OverlayHelpOverlay(OverlayHelpOverlay &&) = delete;
    OverlayHelpOverlay &operator=(OverlayHelpOverlay &&) = delete;

    void Set_content(core::OverlayHelpContent const *content) noexcept;
    [[nodiscard]] bool Has_content() const noexcept;
    [[nodiscard]] bool Is_visible() const noexcept;
    void Hide() noexcept;
    void Hide_if_selection_unstable(bool selection_stable) noexcept;

    void Toggle_at_cursor(core::PointPx cursor_screen,
                          std::span<const core::MonitorWithBounds> monitors,
                          core::RectPx overlay_rect_screen);
    [[nodiscard]] bool Paint_d2d(ID2D1RenderTarget *rt, IDWriteFactory *dwrite,
                                 ID2D1SolidColorBrush *brush) noexcept;

  private:
    [[nodiscard]] bool Ensure_dwrite_formats(IDWriteFactory *dwrite) noexcept;

    core::OverlayHelpContent const *content_ = nullptr;
    bool visible_ = false;
    std::optional<core::RectPx> monitor_rect_client_ = std::nullopt;
    // DWrite text formats are created lazily and reused across overlay frames.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_title_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_body_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_key_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dwrite_section_;
};

} // namespace greenflame
