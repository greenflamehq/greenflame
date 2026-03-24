#pragma once

#include "greenflame_core/annotation_types.h"

namespace greenflame {

using D2DAnnotationBitmapMap =
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID2D1Bitmap>>;

struct D2DAnnotationDrawContext final {
    ID2D1Factory *factory = nullptr;
    ID2D1SolidColorBrush *solid_brush = nullptr;
    ID2D1StrokeStyle *round_cap_style = nullptr;
    ID2D1StrokeStyle *flat_cap_style = nullptr;
    D2DAnnotationBitmapMap *text_bitmaps = nullptr;
    D2DAnnotationBitmapMap *bubble_bitmaps = nullptr;
};

struct D2DAnnotationRenderResources final {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solid_brush;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> round_cap_style;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> flat_cap_style;
    D2DAnnotationBitmapMap text_bitmaps = {};
    D2DAnnotationBitmapMap bubble_bitmaps = {};

    [[nodiscard]] bool Initialize(ID2D1Factory *factory,
                                  ID2D1RenderTarget *render_target);
    [[nodiscard]] D2DAnnotationDrawContext
    Build_context(ID2D1Factory *factory) noexcept;
    void Clear_cached_bitmaps() noexcept;
};

void Draw_d2d_freehand_points(ID2D1RenderTarget *render_target,
                              D2DAnnotationDrawContext context,
                              std::span<const core::PointPx> points,
                              core::StrokeStyle style,
                              core::FreehandTipShape tip_shape);

void Draw_d2d_annotation(ID2D1RenderTarget *render_target,
                         D2DAnnotationDrawContext context,
                         core::Annotation const &annotation);

} // namespace greenflame
