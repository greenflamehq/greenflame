#pragma once

#include "greenflame_core/rect_px.h"
#include "win/ui_palette.h"

namespace greenflame {

// Colors provided by the overlay paint system for drawing buttons.
struct ButtonDrawContext {
    D2D1_COLOR_F fill_color = kOverlayButtonFillColor;
    D2D1_COLOR_F outline_color = kOverlayButtonOutlineColor;
};

struct OverlayButtonGlyph final {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> alpha_mask = {};

    [[nodiscard]] bool Is_valid() const noexcept {
        return width > 0 && height > 0 &&
               alpha_mask.size() ==
                   static_cast<size_t>(width) * static_cast<size_t>(height);
    }
};

enum class OverlayToolbarGlyphId : uint8_t {
    None = 0,
    Brush,
    Highlighter,
    Line,
    Arrow,
    Rectangle,
    FilledRectangle,
    Help,
    Count,
};

[[nodiscard]] constexpr size_t
Overlay_toolbar_glyph_index(OverlayToolbarGlyphId glyph) noexcept {
    return static_cast<size_t>(glyph);
}

// Abstract toolbar button: draws itself and responds to mouse events.
class IOverlayButton {
  public:
    virtual ~IOverlayButton() = default;

    virtual void Draw_d2d(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush,
                          ID2D1Bitmap *glyph, ButtonDrawContext const &ctx) const = 0;

    [[nodiscard]] virtual core::RectPx Bounds() const = 0;
    [[nodiscard]] virtual bool Hit_test(core::PointPx pt) const = 0;
    [[nodiscard]] virtual bool Is_hovered() const = 0;
    [[nodiscard]] virtual bool Is_active() const = 0;
    [[nodiscard]] virtual bool Is_pressed() const noexcept { return false; }

    virtual void Set_position(core::PointPx top_left) = 0;
    virtual void Set_active(bool active) = 0;

    virtual void On_mouse_enter() {}
    virtual void On_mouse_leave() {}
    virtual void On_mouse_down(core::PointPx pt) { (void)pt; }
    virtual void On_mouse_up(core::PointPx pt) { (void)pt; }
};

// Concrete round toolbar button with either a text label or an alpha-mask glyph.
// Supports normal, active (inverted colors), and pressed (yellow-green fill) states.
class OverlayButton final : public IOverlayButton {
  public:
    OverlayButton(core::PointPx position, int diameter, std::wstring label,
                  bool is_toggle = false, bool active = false);
    OverlayButton(core::PointPx position, int diameter, OverlayButtonGlyph const *glyph,
                  bool is_toggle = false, bool active = false);

    void Draw_d2d(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush,
                  ID2D1Bitmap *glyph, ButtonDrawContext const &ctx) const override;
    [[nodiscard]] core::RectPx Bounds() const override;
    [[nodiscard]] bool Hit_test(core::PointPx pt) const override;
    [[nodiscard]] bool Is_hovered() const override { return hovered_; }
    [[nodiscard]] bool Is_active() const override { return active_; }
    [[nodiscard]] bool Is_pressed() const noexcept override { return pressed_; }
    void Set_position(core::PointPx top_left) override;
    void Set_active(bool active) override { active_ = active; }
    void On_mouse_enter() override { hovered_ = true; }
    void On_mouse_leave() override {
        hovered_ = false;
        pressed_ = false;
    }
    void On_mouse_down(core::PointPx pt) override;
    void On_mouse_up(core::PointPx pt) override;

  private:
    core::PointPx position_;
    int diameter_;
    std::wstring label_;
    bool is_toggle_;
    bool hovered_ = false;
    bool active_ = false;
    bool pressed_ = false;
};

} // namespace greenflame
