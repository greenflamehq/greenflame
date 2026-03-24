#include "greenflame/win/d2d_annotation_draw.h"

#include "greenflame/win/d2d_draw_helpers.h"

namespace greenflame {

namespace {

constexpr float kDefaultDpi = 96.f;
constexpr float kStrokeMiterLimit = 10.f;

template <typename T>
[[nodiscard]] bool Bitmap_data_is_valid(T const &annotation) noexcept {
    if (annotation.bitmap_width_px <= 0 || annotation.bitmap_height_px <= 0 ||
        annotation.bitmap_row_bytes < annotation.bitmap_width_px * 4) {
        return false;
    }
    size_t const required_size = static_cast<size_t>(annotation.bitmap_row_bytes) *
                                 static_cast<size_t>(annotation.bitmap_height_px);
    return annotation.premultiplied_bgra.size() >= required_size;
}

void Draw_line(ID2D1RenderTarget *render_target, D2DAnnotationDrawContext context,
               core::LineAnnotation const &line) {
    if (render_target == nullptr || context.solid_brush == nullptr ||
        context.flat_cap_style == nullptr || context.factory == nullptr) {
        return;
    }

    context.solid_brush->SetColor(Colorref_to_d2d(
        line.style.color, Alpha_from_opacity_percent(line.style.opacity_percent)));
    float const stroke_width = static_cast<float>(line.style.width_px);

    if (!line.arrow_head) {
        render_target->DrawLine(Pt(line.start), Pt(line.end), context.solid_brush,
                                stroke_width, context.flat_cap_style);
        return;
    }

    float const dx = static_cast<float>(line.end.x - line.start.x);
    float const dy = static_cast<float>(line.end.y - line.start.y);
    float const length = std::sqrtf(dx * dx + dy * dy);
    if (length < 1.f) {
        render_target->DrawLine(Pt(line.start), Pt(line.end), context.solid_brush,
                                stroke_width, context.flat_cap_style);
        return;
    }

    float const ux = dx / length;
    float const uy = dy / length;
    float const raw_head_length =
        kArrowBaseLength + stroke_width * kArrowLengthPerStroke;
    float const head_length = std::min(
        length, std::max(stroke_width,
                         raw_head_length - stroke_width * kArrowOverlapPerStroke));
    float const head_half =
        (kArrowBaseWidth + stroke_width * kArrowWidthPerStroke) * 0.5f;

    float const ex = static_cast<float>(line.end.x);
    float const ey = static_cast<float>(line.end.y);
    D2D1_POINT_2F const tip = D2D1::Point2F(ex + ux * kHalfPixel, ey + uy * kHalfPixel);
    D2D1_POINT_2F const base_center =
        D2D1::Point2F(ex - ux * head_length, ey - uy * head_length);
    D2D1_POINT_2F const bottom_left =
        D2D1::Point2F(base_center.x + uy * head_half, base_center.y - ux * head_half);
    D2D1_POINT_2F const bottom_right =
        D2D1::Point2F(base_center.x - uy * head_half, base_center.y + ux * head_half);

    D2D1_POINT_2F const shaft_end =
        D2D1::Point2F(base_center.x + ux * stroke_width * kHalfPixel,
                      base_center.y + uy * stroke_width * kHalfPixel);
    render_target->DrawLine(Pt(line.start), shaft_end, context.solid_brush,
                            stroke_width, context.flat_cap_style);

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> arrow;
    if (FAILED(context.factory->CreatePathGeometry(arrow.GetAddressOf()))) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(arrow->Open(sink.GetAddressOf()))) {
        return;
    }
    sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(bottom_left);
    sink->AddLine(bottom_right);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    render_target->FillGeometry(arrow.Get(), context.solid_brush);
}

void Draw_rectangle(ID2D1RenderTarget *render_target, D2DAnnotationDrawContext context,
                    core::RectangleAnnotation const &rect) {
    if (render_target == nullptr || context.solid_brush == nullptr ||
        context.flat_cap_style == nullptr) {
        return;
    }

    context.solid_brush->SetColor(Colorref_to_d2d(
        rect.style.color, Alpha_from_opacity_percent(rect.style.opacity_percent)));
    D2D1_RECT_F const rect_f = Rect(rect.outer_bounds);
    if (rect.filled) {
        render_target->FillRectangle(rect_f, context.solid_brush);
        return;
    }

    float const half_width = static_cast<float>(rect.style.width_px) * 0.5f;
    D2D1_RECT_F const inset =
        D2D1::RectF(rect_f.left + half_width, rect_f.top + half_width,
                    rect_f.right - half_width, rect_f.bottom - half_width);
    if (inset.left >= inset.right || inset.top >= inset.bottom) {
        render_target->FillRectangle(rect_f, context.solid_brush);
        return;
    }

    render_target->DrawRectangle(inset, context.solid_brush,
                                 static_cast<float>(rect.style.width_px),
                                 context.flat_cap_style);
}

void Draw_ellipse(ID2D1RenderTarget *render_target, D2DAnnotationDrawContext context,
                  core::EllipseAnnotation const &ellipse) {
    if (render_target == nullptr || context.solid_brush == nullptr ||
        context.flat_cap_style == nullptr) {
        return;
    }

    context.solid_brush->SetColor(
        Colorref_to_d2d(ellipse.style.color,
                        Alpha_from_opacity_percent(ellipse.style.opacity_percent)));
    D2D1_RECT_F const rect_f = Rect(ellipse.outer_bounds);
    float const rx = (rect_f.right - rect_f.left) * 0.5f;
    float const ry = (rect_f.bottom - rect_f.top) * 0.5f;
    D2D1_ELLIPSE const shape =
        D2D1::Ellipse(D2D1::Point2F(rect_f.left + rx, rect_f.top + ry),
                      std::max(0.0f, rx), std::max(0.0f, ry));

    if (ellipse.filled) {
        render_target->FillEllipse(shape, context.solid_brush);
        return;
    }

    float const stroke_width = static_cast<float>(ellipse.style.width_px);
    float const inset_rx = rx - stroke_width * 0.5f;
    float const inset_ry = ry - stroke_width * 0.5f;
    if (inset_rx <= 0.0f || inset_ry <= 0.0f) {
        render_target->FillEllipse(shape, context.solid_brush);
        return;
    }

    render_target->DrawEllipse(D2D1::Ellipse(shape.point, inset_rx, inset_ry),
                               context.solid_brush, stroke_width,
                               context.flat_cap_style);
}

void Draw_text(ID2D1RenderTarget *render_target, D2DAnnotationDrawContext context,
               uint64_t annotation_id, core::TextAnnotation const &annotation) {
    if (render_target == nullptr || context.text_bitmaps == nullptr ||
        !Bitmap_data_is_valid(annotation)) {
        return;
    }

    auto it = context.text_bitmaps->find(annotation_id);
    if (it == context.text_bitmaps->end()) {
        D2D1_BITMAP_PROPERTIES props{};
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                              D2D1_ALPHA_MODE_PREMULTIPLIED);
        // Match the render target's 96 DPI so DrawBitmap places pixels 1:1.
        props.dpiX = kDefaultDpi;
        props.dpiY = kDefaultDpi;

        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        HRESULT const hr = render_target->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(annotation.bitmap_width_px),
                        static_cast<UINT32>(annotation.bitmap_height_px)),
            annotation.premultiplied_bgra.data(),
            static_cast<UINT32>(annotation.bitmap_row_bytes), props,
            bitmap.GetAddressOf());
        if (FAILED(hr) || !bitmap) {
            return;
        }
        it = context.text_bitmaps->emplace(annotation_id, std::move(bitmap)).first;
    }

    render_target->DrawBitmap(it->second.Get(), Rect(annotation.visual_bounds), 1.f,
                              D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

void Draw_bubble(ID2D1RenderTarget *render_target, D2DAnnotationDrawContext context,
                 uint64_t annotation_id, core::BubbleAnnotation const &annotation) {
    if (render_target == nullptr || context.bubble_bitmaps == nullptr ||
        !Bitmap_data_is_valid(annotation)) {
        return;
    }

    int32_t const radius = annotation.diameter_px / 2;
    core::RectPx const bounds = core::RectPx::From_ltrb(
        annotation.center.x - radius, annotation.center.y - radius,
        annotation.center.x - radius + annotation.diameter_px,
        annotation.center.y - radius + annotation.diameter_px);

    auto it = context.bubble_bitmaps->find(annotation_id);
    if (it == context.bubble_bitmaps->end()) {
        D2D1_BITMAP_PROPERTIES props{};
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                              D2D1_ALPHA_MODE_PREMULTIPLIED);
        // Match the render target's 96 DPI so DrawBitmap places pixels 1:1.
        props.dpiX = kDefaultDpi;
        props.dpiY = kDefaultDpi;

        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        HRESULT const hr = render_target->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(annotation.bitmap_width_px),
                        static_cast<UINT32>(annotation.bitmap_height_px)),
            annotation.premultiplied_bgra.data(),
            static_cast<UINT32>(annotation.bitmap_row_bytes), props,
            bitmap.GetAddressOf());
        if (FAILED(hr) || !bitmap) {
            return;
        }
        it = context.bubble_bitmaps->emplace(annotation_id, std::move(bitmap)).first;
    }

    render_target->DrawBitmap(it->second.Get(), Rect(bounds), 1.f,
                              D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

} // namespace

bool D2DAnnotationRenderResources::Initialize(ID2D1Factory *factory,
                                              ID2D1RenderTarget *render_target) {
    if (factory == nullptr || render_target == nullptr) {
        return false;
    }

    HRESULT hr = render_target->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f), solid_brush.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    {
        D2D1_STROKE_STYLE_PROPERTIES props{};
        props.startCap = D2D1_CAP_STYLE_ROUND;
        props.endCap = D2D1_CAP_STYLE_ROUND;
        props.dashCap = D2D1_CAP_STYLE_ROUND;
        props.lineJoin = D2D1_LINE_JOIN_ROUND;
        props.miterLimit = kStrokeMiterLimit;
        props.dashStyle = D2D1_DASH_STYLE_SOLID;
        hr = factory->CreateStrokeStyle(props, nullptr, 0,
                                        round_cap_style.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    {
        D2D1_STROKE_STYLE_PROPERTIES props{};
        props.startCap = D2D1_CAP_STYLE_FLAT;
        props.endCap = D2D1_CAP_STYLE_FLAT;
        props.dashCap = D2D1_CAP_STYLE_FLAT;
        props.lineJoin = D2D1_LINE_JOIN_MITER;
        props.miterLimit = kStrokeMiterLimit;
        props.dashStyle = D2D1_DASH_STYLE_SOLID;
        hr = factory->CreateStrokeStyle(props, nullptr, 0,
                                        flat_cap_style.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
    }

    return true;
}

D2DAnnotationDrawContext
D2DAnnotationRenderResources::Build_context(ID2D1Factory *factory) noexcept {
    return D2DAnnotationDrawContext{
        .factory = factory,
        .solid_brush = solid_brush.Get(),
        .round_cap_style = round_cap_style.Get(),
        .flat_cap_style = flat_cap_style.Get(),
        .text_bitmaps = &text_bitmaps,
        .bubble_bitmaps = &bubble_bitmaps,
    };
}

void D2DAnnotationRenderResources::Clear_cached_bitmaps() noexcept {
    text_bitmaps.clear();
    bubble_bitmaps.clear();
}

void Draw_d2d_freehand_points(ID2D1RenderTarget *render_target,
                              D2DAnnotationDrawContext context,
                              std::span<const core::PointPx> points,
                              core::StrokeStyle style,
                              core::FreehandTipShape tip_shape) {
    if (render_target == nullptr || context.factory == nullptr ||
        context.solid_brush == nullptr || context.round_cap_style == nullptr ||
        points.empty()) {
        return;
    }

    context.solid_brush->SetColor(Colorref_to_d2d(
        style.color, Alpha_from_opacity_percent(style.opacity_percent)));
    if (points.size() == 1) {
        float const half_extent = static_cast<float>(std::max<int32_t>(
                                      core::StrokeStyle::kMinWidthPx, style.width_px)) *
                                  0.5f;
        float const cx = static_cast<float>(points.front().x);
        float const cy = static_cast<float>(points.front().y);
        if (tip_shape == core::FreehandTipShape::Square) {
            render_target->FillRectangle(D2D1::RectF(cx - half_extent, cy - half_extent,
                                                     cx + half_extent,
                                                     cy + half_extent),
                                         context.solid_brush);
        } else {
            render_target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx, cy), half_extent, half_extent),
                context.solid_brush);
        }
        return;
    }

    if (tip_shape == core::FreehandTipShape::Square) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        if (FAILED(context.factory->CreatePathGeometry(path.GetAddressOf()))) {
            return;
        }
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(path->Open(sink.GetAddressOf()))) {
            return;
        }
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);

        float const half_extent = static_cast<float>(std::max<int32_t>(
                                      core::StrokeStyle::kMinWidthPx, style.width_px)) *
                                  0.5f;
        for (size_t index = 1; index < points.size(); ++index) {
            float const ax = static_cast<float>(points[index - 1].x);
            float const ay = static_cast<float>(points[index - 1].y);
            float const bx = static_cast<float>(points[index].x);
            float const by = static_cast<float>(points[index].y);
            std::array<D2D1_POINT_2F, 8> const corners = {
                D2D1::Point2F(ax - half_extent, ay - half_extent),
                D2D1::Point2F(ax + half_extent, ay - half_extent),
                D2D1::Point2F(ax + half_extent, ay + half_extent),
                D2D1::Point2F(ax - half_extent, ay + half_extent),
                D2D1::Point2F(bx - half_extent, by - half_extent),
                D2D1::Point2F(bx + half_extent, by - half_extent),
                D2D1::Point2F(bx + half_extent, by + half_extent),
                D2D1::Point2F(bx - half_extent, by + half_extent),
            };
            HullResult const hull = Build_convex_hull(corners);
            if (hull.count < 3) {
                continue;
            }

            sink->BeginFigure(hull.points[0], D2D1_FIGURE_BEGIN_FILLED);
            for (size_t hull_index = 1; hull_index < hull.count; ++hull_index) {
                sink->AddLine(hull.points[hull_index]);
            }
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        }
        sink->Close();
        render_target->FillGeometry(path.Get(), context.solid_brush);
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
    if (FAILED(context.factory->CreatePathGeometry(path.GetAddressOf()))) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(sink.GetAddressOf()))) {
        return;
    }
    sink->BeginFigure(Pt(points[0]), D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t index = 1; index < points.size(); ++index) {
        sink->AddLine(Pt(points[index]));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    render_target->DrawGeometry(path.Get(), context.solid_brush,
                                static_cast<float>(style.width_px),
                                context.round_cap_style);
}

void Draw_d2d_annotation(ID2D1RenderTarget *render_target,
                         D2DAnnotationDrawContext context,
                         core::Annotation const &annotation) {
    std::visit(core::Overloaded{
                   [&](core::FreehandStrokeAnnotation const &freehand) {
                       Draw_d2d_freehand_points(render_target, context, freehand.points,
                                                freehand.style,
                                                freehand.freehand_tip_shape);
                   },
                   [&](core::LineAnnotation const &line) {
                       Draw_line(render_target, context, line);
                   },
                   [&](core::RectangleAnnotation const &rect) {
                       Draw_rectangle(render_target, context, rect);
                   },
                   [&](core::EllipseAnnotation const &ellipse) {
                       Draw_ellipse(render_target, context, ellipse);
                   },
                   [&](core::TextAnnotation const &text) {
                       Draw_text(render_target, context, annotation.id, text);
                   },
                   [&](core::BubbleAnnotation const &bubble) {
                       Draw_bubble(render_target, context, annotation.id, bubble);
                   },
               },
               annotation.data);
}

} // namespace greenflame
