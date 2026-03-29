#pragma once

#include "greenflame_core/text_layout_engine.h"

struct IDWriteFactory;

namespace greenflame {

class D2DTextLayoutEngine final : public core::ITextLayoutEngine {
  public:
    D2DTextLayoutEngine(ID2D1Factory *d2d_factory,
                        IDWriteFactory *dwrite_factory) noexcept;
    void Set_font_families(std::array<std::wstring_view, 4> families);
    void Set_target_dpi(float dpi) noexcept;
    [[nodiscard]] float Target_dpi() const noexcept;

    [[nodiscard]] int32_t
    Line_ascent(core::TextAnnotationBaseStyle const &style) override;
    [[nodiscard]] core::DraftTextLayoutResult
    Build_draft_layout(core::TextDraftBuffer const &buf, core::PointPx origin) override;
    [[nodiscard]] int32_t Hit_test_point(core::TextDraftBuffer const &buf,
                                         core::PointPx origin,
                                         core::PointPx point) override;
    [[nodiscard]] int32_t Move_vertical(core::TextDraftBuffer const &buf,
                                        core::PointPx origin, int32_t offset,
                                        int delta_lines,
                                        int32_t preferred_x_px) override;
    [[nodiscard]] bool Prepare_for_cli(core::TextAnnotation &annotation);
    void Rasterize(core::TextAnnotation &annotation) override;
    void Rasterize_bubble(core::BubbleAnnotation &annotation) override;

  private:
    static constexpr float kDefaultTargetDpi = 96.0f;
    ID2D1Factory *d2d_factory_ = nullptr;
    IDWriteFactory *dwrite_factory_ = nullptr;
    std::array<std::wstring, 4> font_families_ = {};
    float target_dpi_ = kDefaultTargetDpi;
};

} // namespace greenflame
