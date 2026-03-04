#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame {

// Colors provided by the overlay paint system for drawing buttons.
struct ButtonDrawContext {
    COLORREF fill_color = RGB(217, 240, 227); // kCoordTooltipBg
    COLORREF outline_color = RGB(26, 121, 6); // kCoordTooltipText
};

// Abstract toolbar button: draws itself and responds to mouse events.
class IOverlayButton {
  public:
    virtual ~IOverlayButton() = default;

    virtual void Draw(HDC dc, ButtonDrawContext const &ctx) const = 0;

    [[nodiscard]] virtual core::RectPx Bounds() const = 0;
    [[nodiscard]] virtual bool Hit_test(core::PointPx pt) const = 0;
    [[nodiscard]] virtual bool Is_hovered() const = 0;

    virtual void Set_position(core::PointPx top_left) = 0;

    virtual void On_mouse_enter() {}
    virtual void On_mouse_leave() {}
    virtual void On_mouse_down(core::PointPx pt) { (void)pt; }
    virtual void On_mouse_up(core::PointPx pt) { (void)pt; }
};

// Debug placeholder: numbered circle (1-based label). Hover shown as white inner ring.
class DebugNumberButton final : public IOverlayButton {
  public:
    DebugNumberButton(core::PointPx position, int diameter, int zero_based_index);

    void Draw(HDC dc, ButtonDrawContext const &ctx) const override;
    [[nodiscard]] core::RectPx Bounds() const override;
    [[nodiscard]] bool Hit_test(core::PointPx pt) const override;
    [[nodiscard]] bool Is_hovered() const override { return hovered_; }
    void Set_position(core::PointPx top_left) override;
    void On_mouse_enter() override { hovered_ = true; }
    void On_mouse_leave() override { hovered_ = false; }

  private:
    core::PointPx position_;
    int diameter_;
    int index_; // 0-based; displayed as index+1
    bool hovered_ = false;
};

} // namespace greenflame
