// Overlay toolbar button: round labelled or glyph-backed button with hover,
// active, and pressed states.

#include "win/overlay_button.h"

namespace greenflame {

namespace {

constexpr float kButtonPenWidthD2D = 1.5f;
constexpr float kButtonHoverRingWidthD2D = 3.0f;
constexpr float kButtonGlyphInsetD2D = 8.0f;
constexpr D2D1_COLOR_F kPressedFillColor = {155.f / 255.f, 220.f / 255.f, 65.f / 255.f,
                                            1.f};

[[nodiscard]] D2D1_COLOR_F Blend_button_colors(D2D1_COLOR_F a, D2D1_COLOR_F b,
                                               float a_weight,
                                               float b_weight) noexcept {
    float const total_weight = a_weight + b_weight;
    return {(a.r * a_weight + b.r * b_weight) / total_weight,
            (a.g * a_weight + b.g * b_weight) / total_weight,
            (a.b * a_weight + b.b * b_weight) / total_weight,
            (a.a * a_weight + b.a * b_weight) / total_weight};
}

} // namespace

OverlayButton::OverlayButton(core::PointPx position, int diameter, std::wstring label,
                             bool is_toggle, bool active)
    : position_(position), diameter_(diameter), label_(std::move(label)),
      is_toggle_(is_toggle), active_(active) {}

OverlayButton::OverlayButton(core::PointPx position, int diameter,
                             OverlayButtonGlyph const * /*glyph*/, bool is_toggle,
                             bool active)
    : position_(position), diameter_(diameter), is_toggle_(is_toggle), active_(active) {
}

core::RectPx OverlayButton::Bounds() const {
    return core::RectPx::From_ltrb(position_.x, position_.y, position_.x + diameter_,
                                   position_.y + diameter_);
}

bool OverlayButton::Hit_test(core::PointPx pt) const {
    int const cx = position_.x + diameter_ / 2;
    int const cy = position_.y + diameter_ / 2;
    int const r = diameter_ / 2;
    int const dx = pt.x - cx;
    int const dy = pt.y - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

void OverlayButton::Set_position(core::PointPx top_left) { position_ = top_left; }

void OverlayButton::On_mouse_down(core::PointPx /*pt*/) { pressed_ = true; }

void OverlayButton::On_mouse_up(core::PointPx /*pt*/) {
    pressed_ = false;
    if (is_toggle_) {
        active_ = !active_;
    }
}

void OverlayButton::Draw_d2d(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush,
                             ID2D1Bitmap *glyph_bitmap,
                             ButtonDrawContext const &ctx) const {
    if (!rt || !brush) {
        return;
    }

    D2D1_COLOR_F fill_col{};
    D2D1_COLOR_F outline_col{};
    D2D1_COLOR_F text_col{};
    if (pressed_) {
        fill_col = kPressedFillColor;
        outline_col = ctx.outline_color;
        text_col = ctx.outline_color;
    } else if (active_) {
        fill_col = ctx.outline_color;
        outline_col = ctx.fill_color;
        text_col = ctx.fill_color;
    } else {
        fill_col = ctx.fill_color;
        outline_col = ctx.outline_color;
        text_col = ctx.outline_color;
    }

    float const df = static_cast<float>(diameter_);
    float const left = static_cast<float>(position_.x);
    float const top = static_cast<float>(position_.y);
    D2D1_ELLIPSE const ell = D2D1::Ellipse(
        D2D1::Point2F(left + df / 2.f, top + df / 2.f), df / 2.f, df / 2.f);

    brush->SetColor(fill_col);
    rt->FillEllipse(ell, brush);

    D2D1_ELLIPSE const outline_ell =
        D2D1::Ellipse(ell.point, ell.radiusX - kButtonPenWidthD2D / 2.f,
                      ell.radiusY - kButtonPenWidthD2D / 2.f);
    brush->SetColor(outline_col);
    rt->DrawEllipse(outline_ell, brush, kButtonPenWidthD2D);

    if (hovered_) {
        D2D1_COLOR_F const hover_col =
            Blend_button_colors(outline_col, fill_col, 3.f, 1.f);
        D2D1_ELLIPSE const hover_ell =
            D2D1::Ellipse(ell.point, ell.radiusX - kButtonHoverRingWidthD2D / 2.f,
                          ell.radiusY - kButtonHoverRingWidthD2D / 2.f);
        brush->SetColor(hover_col);
        rt->DrawEllipse(hover_ell, brush, kButtonHoverRingWidthD2D);
    }

    if (glyph_bitmap) {
        float const inset = kButtonGlyphInsetD2D;
        D2D1_SIZE_F const glyph_size = glyph_bitmap->GetSize();
        float const max_extent = std::max(1.f, df - 2.f * inset);
        float const scale =
            std::min(max_extent / glyph_size.width, max_extent / glyph_size.height);
        float const draw_w = glyph_size.width * scale;
        float const draw_h = glyph_size.height * scale;
        float const draw_left = left + (df - draw_w) / 2.f;
        float const draw_top = top + (df - draw_h) / 2.f;
        D2D1_RECT_F const dest =
            D2D1::RectF(draw_left, draw_top, draw_left + draw_w, draw_top + draw_h);
        D2D1_RECT_F const src =
            D2D1::RectF(0.f, 0.f, glyph_size.width, glyph_size.height);
        brush->SetColor(text_col);
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        rt->FillOpacityMask(glyph_bitmap, brush, D2D1_OPACITY_MASK_CONTENT_GRAPHICS,
                            &dest, &src);
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
}

} // namespace greenflame
