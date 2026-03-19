#include "greenflame/win/d2d_paint.h"

#include "greenflame/win/d2d_overlay_resources.h"
#include "greenflame/win/overlay_button.h"
#include "greenflame/win/overlay_help_overlay.h"
#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/color_wheel.h"
#include "greenflame_core/selection_handles.h"
#include "win/ui_palette.h"

namespace greenflame {

namespace {

constexpr float kColorChannelMaxF = 255.f;
constexpr float kArrowBaseWidth = 10.0f;
constexpr float kArrowBaseLength = 18.0f;
constexpr float kArrowWidthPerStroke = 2.0f;
constexpr float kArrowLengthPerStroke = 4.0f;
constexpr float kArrowOverlapPerStroke = 2.0f;
constexpr float kHalfPixel = 0.5f;
constexpr float kTextMeasureMaxExtent = 8192.f;
constexpr float kMagnifierBorderInset = 0.75f;
constexpr float kMagnifierBorderStrokeWidth = 1.5f;
constexpr float kOverlayDimAlpha = 0.5f;
constexpr float kDefaultDpi = 96.f;
constexpr float kDraftTextSelectionAlpha = 0.7f;
constexpr float kDraftTextOverwriteCaretAlpha = 0.65f;
constexpr float kColorWheelFontPreviewPointSize = 18.f;
constexpr float kColorWheelFontPreviewLayoutPaddingPx = 4.f;
// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

inline D2D1_POINT_2F Pt(core::PointPx p) {
    return D2D1::Point2F(static_cast<float>(p.x), static_cast<float>(p.y));
}

inline D2D1_RECT_F Rect(core::RectPx r) {
    return D2D1::RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                       static_cast<float>(r.right), static_cast<float>(r.bottom));
}

inline D2D1_COLOR_F Colorref_to_d2d(COLORREF c, float alpha = 1.f) {
    return D2D1::ColorF(static_cast<float>(GetRValue(c)) / kColorChannelMaxF,
                        static_cast<float>(GetGValue(c)) / kColorChannelMaxF,
                        static_cast<float>(GetBValue(c)) / kColorChannelMaxF, alpha);
}

[[nodiscard]] constexpr D2D1_COLOR_F With_alpha(D2D1_COLOR_F color,
                                                float alpha) noexcept {
    return {color.r, color.g, color.b, alpha};
}

[[nodiscard]] std::wstring_view
Resolve_text_font_family(core::TextFontChoice choice,
                         std::array<std::wstring_view, 4> const &families) noexcept {
    size_t const index = core::Text_font_choice_index(choice);
    if (families[index].empty()) {
        return core::kDefaultTextFontFamilies[index];
    }
    return families[index];
}

[[nodiscard]] float Alpha_from_opacity_percent(int32_t opacity_percent) noexcept {
    int32_t const clamped =
        std::clamp(opacity_percent, core::StrokeStyle::kMinOpacityPercent,
                   core::StrokeStyle::kMaxOpacityPercent);
    return static_cast<float>(clamped) / 100.f;
}

[[nodiscard]] float Cross(D2D1_POINT_2F origin, D2D1_POINT_2F a,
                          D2D1_POINT_2F b) noexcept {
    float const ax = a.x - origin.x;
    float const ay = a.y - origin.y;
    float const bx = b.x - origin.x;
    float const by = b.y - origin.y;
    return ax * by - ay * bx;
}

struct HullResult {
    std::array<D2D1_POINT_2F, 8> points = {};
    size_t count = 0;
};

[[nodiscard]] HullResult Build_convex_hull(std::array<D2D1_POINT_2F, 8> points) {
    std::sort(points.begin(), points.end(), [](D2D1_POINT_2F a, D2D1_POINT_2F b) {
        return std::tie(a.x, a.y) < std::tie(b.x, b.y);
    });
    auto const last =
        std::unique(points.begin(), points.end(), [](D2D1_POINT_2F a, D2D1_POINT_2F b) {
            return !(a.x < b.x) && !(b.x < a.x) && !(a.y < b.y) && !(b.y < a.y);
        });
    size_t const count = static_cast<size_t>(std::distance(points.begin(), last));
    std::array<D2D1_POINT_2F, 16> working = {};
    size_t working_size = 0;

    for (size_t i = 0; i < count; ++i) {
        while (working_size >= 2 &&
               Cross(working[working_size - 2], working[working_size - 1], points[i]) <=
                   0.f) {
            --working_size;
        }
        working[working_size++] = points[i];
    }

    size_t const lower_size = working_size;
    if (count > 1) {
        for (size_t i = count - 1; i > 0; --i) {
            while (working_size > lower_size &&
                   Cross(working[working_size - 2], working[working_size - 1],
                         points[i - 1]) <= 0.f) {
                --working_size;
            }
            working[working_size++] = points[i - 1];
        }
    }

    if (working_size > 1) {
        --working_size;
    }

    HullResult result{};
    result.count = working_size;
    for (size_t i = 0; i < working_size; ++i) {
        result.points[i] = working[i];
    }
    return result;
}

// ---------------------------------------------------------------------------
// Annotation drawing helpers
// ---------------------------------------------------------------------------

void Draw_freehand_points(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                          std::span<const core::PointPx> points,
                          core::StrokeStyle style, core::FreehandTipShape tip_shape) {
    if (points.empty()) {
        return;
    }
    res.solid_brush->SetColor(Colorref_to_d2d(
        style.color, Alpha_from_opacity_percent(style.opacity_percent)));
    if (points.size() == 1) {
        float const half_extent = static_cast<float>(std::max<int32_t>(
                                      core::StrokeStyle::kMinWidthPx, style.width_px)) /
                                  2.f;
        float const cx = static_cast<float>(points.front().x);
        float const cy = static_cast<float>(points.front().y);
        if (tip_shape == core::FreehandTipShape::Square) {
            rt->FillRectangle(D2D1::RectF(cx - half_extent, cy - half_extent,
                                          cx + half_extent, cy + half_extent),
                              res.solid_brush.Get());
        } else {
            rt->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx, cy), half_extent, half_extent),
                res.solid_brush.Get());
        }
        return;
    }

    if (tip_shape == core::FreehandTipShape::Square) {
        // Match the axis-aligned square brush model by filling the convex hull of the
        // endpoint squares for each segment. This avoids D2D stroke-join artifacts.
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        if (FAILED(res.factory->CreatePathGeometry(path.GetAddressOf()))) {
            return;
        }
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(path->Open(sink.GetAddressOf()))) {
            return;
        }
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);

        float const half_extent = static_cast<float>(std::max<int32_t>(
                                      core::StrokeStyle::kMinWidthPx, style.width_px)) /
                                  2.f;
        for (size_t i = 1; i < points.size(); ++i) {
            float const ax = static_cast<float>(points[i - 1].x);
            float const ay = static_cast<float>(points[i - 1].y);
            float const bx = static_cast<float>(points[i].x);
            float const by = static_cast<float>(points[i].y);
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
        rt->FillGeometry(path.Get(), res.solid_brush.Get());
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
    if (FAILED(res.factory->CreatePathGeometry(path.GetAddressOf()))) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(sink.GetAddressOf()))) {
        return;
    }
    sink->BeginFigure(Pt(points[0]), D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < points.size(); ++i) {
        sink->AddLine(Pt(points[i]));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    rt->DrawGeometry(path.Get(), res.solid_brush.Get(),
                     static_cast<float>(style.width_px), res.round_cap_style.Get());
}

void Draw_freehand(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                   core::FreehandStrokeAnnotation const &fh) {
    Draw_freehand_points(rt, res, fh.points, fh.style, fh.freehand_tip_shape);
}

void Draw_line(ID2D1RenderTarget *rt, D2DOverlayResources &res,
               core::LineAnnotation const &line) {
    res.solid_brush->SetColor(Colorref_to_d2d(
        line.style.color, Alpha_from_opacity_percent(line.style.opacity_percent)));
    float const w = static_cast<float>(line.style.width_px);

    if (!line.arrow_head) {
        // Plain line: square (flat) ends to match annotation_hit_test geometry.
        rt->DrawLine(Pt(line.start), Pt(line.end), res.solid_brush.Get(), w,
                     res.flat_cap_style.Get());
        return;
    }

    float const dx = static_cast<float>(line.end.x - line.start.x);
    float const dy = static_cast<float>(line.end.y - line.start.y);
    float const len = std::sqrtf(dx * dx + dy * dy);
    if (len < 1.f) {
        rt->DrawLine(Pt(line.start), Pt(line.end), res.solid_brush.Get(), w,
                     res.flat_cap_style.Get());
        return;
    }
    float const ux = dx / len; // unit vector along line
    float const uy = dy / len;

    float const raw_head_length = kArrowBaseLength + w * kArrowLengthPerStroke;
    float const head_length =
        std::min(len, std::max(w, raw_head_length - w * kArrowOverlapPerStroke));
    float const head_half = (kArrowBaseWidth + w * kArrowWidthPerStroke) / 2.f;

    // Tip is 0.5 px past line.end along the axis (matching annotation_hit_test.cpp).
    float const ex = static_cast<float>(line.end.x);
    float const ey = static_cast<float>(line.end.y);
    D2D1_POINT_2F const tip = D2D1::Point2F(ex + ux * kHalfPixel, ey + uy * kHalfPixel);
    D2D1_POINT_2F const base_center =
        D2D1::Point2F(ex - ux * head_length, ey - uy * head_length);
    D2D1_POINT_2F const bl =
        D2D1::Point2F(base_center.x + uy * head_half, base_center.y - ux * head_half);
    D2D1_POINT_2F const br =
        D2D1::Point2F(base_center.x - uy * head_half, base_center.y + ux * head_half);

    // Extend shaft w/2 past base_center into the head. The triangle is drawn on top
    // and covers the overlap, preventing the 1-px antialiasing gap at the junction.
    D2D1_POINT_2F const shaft_end = D2D1::Point2F(base_center.x + ux * w * kHalfPixel,
                                                  base_center.y + uy * w * kHalfPixel);
    rt->DrawLine(Pt(line.start), shaft_end, res.solid_brush.Get(), w,
                 res.flat_cap_style.Get());

    // Filled arrowhead triangle (drawn after shaft so it covers the overlap).
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> arrow;
    if (FAILED(res.factory->CreatePathGeometry(arrow.GetAddressOf()))) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(arrow->Open(sink.GetAddressOf()))) {
        return;
    }
    sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(bl);
    sink->AddLine(br);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt->FillGeometry(arrow.Get(), res.solid_brush.Get());
}

void Draw_rectangle(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                    core::RectangleAnnotation const &rect) {
    res.solid_brush->SetColor(Colorref_to_d2d(
        rect.style.color, Alpha_from_opacity_percent(rect.style.opacity_percent)));
    D2D1_RECT_F const rf = Rect(rect.outer_bounds);
    if (rect.filled) {
        rt->FillRectangle(rf, res.solid_brush.Get());
    } else {
        float const hw = static_cast<float>(rect.style.width_px) * 0.5f;
        D2D1_RECT_F const inset =
            D2D1::RectF(rf.left + hw, rf.top + hw, rf.right - hw, rf.bottom - hw);
        if (inset.left >= inset.right || inset.top >= inset.bottom) {
            rt->FillRectangle(rf, res.solid_brush.Get());
        } else {
            rt->DrawRectangle(inset, res.solid_brush.Get(),
                              static_cast<float>(rect.style.width_px),
                              res.flat_cap_style.Get());
        }
    }
}

void Draw_ellipse(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                  core::EllipseAnnotation const &ellipse) {
    res.solid_brush->SetColor(
        Colorref_to_d2d(ellipse.style.color,
                        Alpha_from_opacity_percent(ellipse.style.opacity_percent)));
    D2D1_RECT_F const rf = Rect(ellipse.outer_bounds);
    float const rx = (rf.right - rf.left) * 0.5f;
    float const ry = (rf.bottom - rf.top) * 0.5f;
    D2D1_ELLIPSE const shape = D2D1::Ellipse(D2D1::Point2F(rf.left + rx, rf.top + ry),
                                             std::max(0.0f, rx), std::max(0.0f, ry));

    if (ellipse.filled) {
        rt->FillEllipse(shape, res.solid_brush.Get());
        return;
    }

    float const stroke_width = static_cast<float>(ellipse.style.width_px);
    float const inset_rx = rx - (stroke_width * 0.5f);
    float const inset_ry = ry - (stroke_width * 0.5f);
    if (inset_rx <= 0.0f || inset_ry <= 0.0f) {
        rt->FillEllipse(shape, res.solid_brush.Get());
        return;
    }

    rt->DrawEllipse(D2D1::Ellipse(shape.point, inset_rx, inset_ry),
                    res.solid_brush.Get(), stroke_width, res.flat_cap_style.Get());
}

[[nodiscard]] bool
Text_bitmap_is_valid(core::TextAnnotation const &annotation) noexcept {
    if (annotation.bitmap_width_px <= 0 || annotation.bitmap_height_px <= 0 ||
        annotation.bitmap_row_bytes < annotation.bitmap_width_px * 4) {
        return false;
    }
    size_t const required_size = static_cast<size_t>(annotation.bitmap_row_bytes) *
                                 static_cast<size_t>(annotation.bitmap_height_px);
    return annotation.premultiplied_bgra.size() >= required_size;
}

void Draw_text(ID2D1RenderTarget *rt, D2DOverlayResources &res, uint64_t annotation_id,
               core::TextAnnotation const &annotation) {
    if (!rt || !Text_bitmap_is_valid(annotation)) {
        return;
    }

    auto it = res.text_bitmaps.find(annotation_id);
    if (it == res.text_bitmaps.end()) {
        D2D1_BITMAP_PROPERTIES props{};
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                              D2D1_ALPHA_MODE_PREMULTIPLIED);
        props.dpiX = kDefaultDpi;
        props.dpiY = kDefaultDpi;

        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        HRESULT const hr = rt->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(annotation.bitmap_width_px),
                        static_cast<UINT32>(annotation.bitmap_height_px)),
            annotation.premultiplied_bgra.data(),
            static_cast<UINT32>(annotation.bitmap_row_bytes), props,
            bitmap.GetAddressOf());
        if (FAILED(hr) || !bitmap) {
            return;
        }
        it = res.text_bitmaps.emplace(annotation_id, std::move(bitmap)).first;
    }

    rt->DrawBitmap(it->second.Get(), Rect(annotation.visual_bounds), 1.f,
                   D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

void Draw_bubble(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                 uint64_t annotation_id, core::BubbleAnnotation const &annotation) {
    if (rt == nullptr || annotation.bitmap_width_px <= 0 ||
        annotation.bitmap_height_px <= 0 || annotation.premultiplied_bgra.empty()) {
        return;
    }

    int32_t const r = annotation.diameter_px / 2;
    core::RectPx const bounds =
        core::RectPx::From_ltrb(annotation.center.x - r, annotation.center.y - r,
                                annotation.center.x - r + annotation.diameter_px,
                                annotation.center.y - r + annotation.diameter_px);

    auto it = res.bubble_bitmaps.find(annotation_id);
    if (it == res.bubble_bitmaps.end()) {
        D2D1_BITMAP_PROPERTIES props{};
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                              D2D1_ALPHA_MODE_PREMULTIPLIED);
        props.dpiX = kDefaultDpi;
        props.dpiY = kDefaultDpi;

        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        HRESULT const hr = rt->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(annotation.bitmap_width_px),
                        static_cast<UINT32>(annotation.bitmap_height_px)),
            annotation.premultiplied_bgra.data(),
            static_cast<UINT32>(annotation.bitmap_row_bytes), props,
            bitmap.GetAddressOf());
        if (FAILED(hr) || !bitmap) {
            return;
        }
        it = res.bubble_bitmaps.emplace(annotation_id, std::move(bitmap)).first;
    }

    rt->DrawBitmap(it->second.Get(), Rect(bounds), 1.f,
                   D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

void Draw_annotation(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                     core::Annotation const &ann) {
    std::visit(
        core::Overloaded{
            [&](core::FreehandStrokeAnnotation const &fh) {
                Draw_freehand(rt, res, fh);
            },
            [&](core::LineAnnotation const &line) { Draw_line(rt, res, line); },
            [&](core::RectangleAnnotation const &rect) {
                Draw_rectangle(rt, res, rect);
            },
            [&](core::EllipseAnnotation const &ellipse) {
                Draw_ellipse(rt, res, ellipse);
            },
            [&](core::TextAnnotation const &text) { Draw_text(rt, res, ann.id, text); },
            [&](core::BubbleAnnotation const &bubble) {
                Draw_bubble(rt, res, ann.id, bubble);
            },
        },
        ann.data);
}

// ---------------------------------------------------------------------------
// Selection border
// ---------------------------------------------------------------------------

void Draw_selection_border(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                           core::RectPx sel, bool dashed) {
    if (sel.Is_empty()) {
        return;
    }
    res.solid_brush->SetColor(kBorderColor);
    D2D1_RECT_F const rf = Rect(sel);
    float const l = rf.left + kHalfPixel;
    float const r = rf.right - kHalfPixel;
    float const t = rf.top + kHalfPixel;
    float const b = rf.bottom - kHalfPixel;
    ID2D1StrokeStyle *style =
        dashed ? res.dashed_style.Get() : res.flat_cap_style.Get();
    // Four distinct lines (no marquee): horizontals both left-to-right,
    // verticals both top-to-bottom.
    rt->DrawLine(D2D1::Point2F(l, t), D2D1::Point2F(r, t), res.solid_brush.Get(), 1.f,
                 style);
    rt->DrawLine(D2D1::Point2F(l, b), D2D1::Point2F(r, b), res.solid_brush.Get(), 1.f,
                 style);
    rt->DrawLine(D2D1::Point2F(l, t), D2D1::Point2F(l, b), res.solid_brush.Get(), 1.f,
                 style);
    rt->DrawLine(D2D1::Point2F(r, t), D2D1::Point2F(r, b), res.solid_brush.Get(), 1.f,
                 style);
}

// ---------------------------------------------------------------------------
// Text box helper: semi-transparent background, dark border, centered text.
// Returns the text layout metrics for the caller (may be needed for clipping).
// ---------------------------------------------------------------------------

struct TextBoxResult {
    float box_left = 0.f;
    float box_top = 0.f;
    float box_w = 0.f;
    float box_h = 0.f;
};

// Measure text and return layout metrics. Returns empty metrics on failure.
[[nodiscard]] bool Measure_text(D2DOverlayResources &res, IDWriteTextFormat *fmt,
                                std::wstring_view text, float &out_w, float &out_h) {
    if (!res.dwrite_factory || !fmt || text.empty()) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    HRESULT const hr = res.dwrite_factory->CreateTextLayout(
        text.data(), static_cast<UINT32>(text.size()), fmt, kTextMeasureMaxExtent,
        kTextMeasureMaxExtent, layout.GetAddressOf());
    if (FAILED(hr) || !layout) {
        return false;
    }
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) {
        return false;
    }
    out_w = metrics.width;
    out_h = metrics.height;
    return true;
}

void Draw_text_box(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                   IDWriteTextFormat *fmt, std::wstring_view text, float box_left,
                   float box_top, float box_w, float box_h, float margin,
                   DWRITE_TEXT_ALIGNMENT text_align = DWRITE_TEXT_ALIGNMENT_CENTER) {
    if (!rt || !fmt || text.empty()) {
        return;
    }
    // Background
    res.solid_brush->SetColor(With_alpha(kCoordTooltipBg, kCoordTooltipAlpha));
    rt->FillRectangle(D2D1::RectF(box_left, box_top, box_left + box_w, box_top + box_h),
                      res.solid_brush.Get());

    // Border
    res.solid_brush->SetColor(kCoordTooltipText);
    rt->DrawRectangle(D2D1::RectF(box_left + kHalfPixel, box_top + kHalfPixel,
                                  box_left + box_w - kHalfPixel,
                                  box_top + box_h - kHalfPixel),
                      res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());

    // Text
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(res.dwrite_factory->CreateTextLayout(
            text.data(), static_cast<UINT32>(text.size()), fmt, box_w - 2.f * margin,
            box_h - 2.f * margin, layout.GetAddressOf())) ||
        !layout) {
        return;
    }
    layout->SetTextAlignment(text_align);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    res.solid_brush->SetColor(kCoordTooltipText);
    rt->DrawTextLayout(D2D1::Point2F(box_left + margin, box_top + margin), layout.Get(),
                       res.solid_brush.Get());
}

struct StyledTextRange final {
    DWRITE_TEXT_RANGE range = {};
    core::TextStyleFlags flags = {};
};

struct DraftTextLayoutData final {
    std::wstring text = {};
    std::vector<StyledTextRange> styled_ranges = {};
};

[[nodiscard]] DraftTextLayoutData
Build_text_layout_data(std::span<const core::TextRun> runs) {
    DraftTextLayoutData data{};
    UINT32 position = 0;
    for (core::TextRun const &run : runs) {
        data.text += run.text;
        UINT32 const length = static_cast<UINT32>(run.text.size());
        if (length == 0) {
            continue;
        }
        data.styled_ranges.push_back(
            StyledTextRange{DWRITE_TEXT_RANGE{position, length}, run.flags});
        position += length;
    }
    return data;
}

[[nodiscard]] DWRITE_FONT_WEIGHT
Font_weight(core::TextStyleFlags const &flags) noexcept {
    return flags.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
}

[[nodiscard]] DWRITE_FONT_STYLE Font_style(core::TextStyleFlags const &flags) noexcept {
    return flags.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
}

[[nodiscard]] Microsoft::WRL::ComPtr<IDWriteTextFormat>
Create_text_format(IDWriteFactory *factory, std::wstring_view family,
                   float point_size) {
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    if (factory == nullptr || family.empty()) {
        return format;
    }

    HRESULT const hr = factory->CreateTextFormat(
        family.data(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, point_size, L"", format.GetAddressOf());
    if (FAILED(hr) || !format) {
        return {};
    }

    (void)format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    (void)format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    (void)format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return format;
}

[[nodiscard]] Microsoft::WRL::ComPtr<IDWriteTextLayout>
Create_text_layout(IDWriteFactory *factory, IDWriteTextFormat *format,
                   std::wstring_view text, float max_width = kTextMeasureMaxExtent,
                   float max_height = kTextMeasureMaxExtent) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (factory == nullptr || format == nullptr || text.empty()) {
        return layout;
    }

    HRESULT const hr =
        factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format,
                                  max_width, max_height, layout.GetAddressOf());
    if (FAILED(hr) || !layout) {
        return {};
    }
    return layout;
}

void Apply_styled_ranges(IDWriteTextLayout *layout,
                         std::span<const StyledTextRange> styled_ranges) {
    if (layout == nullptr) {
        return;
    }

    for (StyledTextRange const &styled_range : styled_ranges) {
        if (styled_range.range.length == 0) {
            continue;
        }
        (void)layout->SetFontWeight(Font_weight(styled_range.flags),
                                    styled_range.range);
        (void)layout->SetFontStyle(Font_style(styled_range.flags), styled_range.range);
        (void)layout->SetUnderline(styled_range.flags.underline, styled_range.range);
        (void)layout->SetStrikethrough(styled_range.flags.strikethrough,
                                       styled_range.range);
    }
}

[[nodiscard]] bool
Build_draft_text_layout(D2DOverlayResources &res,
                        core::TextAnnotation const &annotation,
                        std::array<std::wstring_view, 4> const &font_families,
                        Microsoft::WRL::ComPtr<IDWriteTextFormat> &format,
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> &layout,
                        DraftTextLayoutData &layout_data) {
    if (!res.dwrite_factory) {
        return false;
    }

    layout_data = Build_text_layout_data(annotation.runs);
    if (layout_data.text.empty()) {
        return false;
    }

    std::wstring_view const family =
        Resolve_text_font_family(annotation.base_style.font_choice, font_families);
    format = Create_text_format(res.dwrite_factory.Get(), family,
                                static_cast<float>(annotation.base_style.point_size));
    if (!format) {
        return false;
    }

    layout =
        Create_text_layout(res.dwrite_factory.Get(), format.Get(), layout_data.text);
    if (!layout) {
        return false;
    }

    Apply_styled_ranges(layout.Get(), layout_data.styled_ranges);
    return true;
}

void Draw_draft_text(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                     D2DPaintInput const &input) {
    core::TextAnnotation const *const annotation = input.draft_text_annotation;
    if (rt == nullptr || annotation == nullptr) {
        return;
    }

    res.solid_brush->SetColor(
        With_alpha(kOverlayButtonFillColor, kDraftTextSelectionAlpha));
    for (core::RectPx const &selection_rect : input.draft_text_selection_rects) {
        if (!selection_rect.Is_empty()) {
            rt->FillRectangle(Rect(selection_rect), res.solid_brush.Get());
        }
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    DraftTextLayoutData layout_data{};
    if (Build_draft_text_layout(res, *annotation, input.color_wheel_font_families,
                                format, layout, layout_data)) {
        res.solid_brush->SetColor(Colorref_to_d2d(annotation->base_style.color));
        rt->DrawTextLayout(Pt(annotation->origin), layout.Get(), res.solid_brush.Get());
    }

    if (!input.draft_text_blink_visible || input.draft_text_caret_rect.Is_empty()) {
        return;
    }

    float const caret_alpha =
        input.draft_text_insert_mode ? 1.f : kDraftTextOverwriteCaretAlpha;
    res.solid_brush->SetColor(
        Colorref_to_d2d(annotation->base_style.color, caret_alpha));
    rt->FillRectangle(Rect(input.draft_text_caret_rect), res.solid_brush.Get());
}

// ---------------------------------------------------------------------------
// Crosshair + coord tooltip (shown when no selection, not dragging)
// ---------------------------------------------------------------------------

void Draw_crosshair(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                    core::PointPx cursor, int vd_width, int vd_height) {
    if (!res.crosshair_style) {
        return;
    }
    float const cx = static_cast<float>(cursor.x);
    float const cy = static_cast<float>(cursor.y);
    res.solid_brush->SetColor(kBorderColor);
    rt->DrawLine(D2D1::Point2F(cx, 0.f),
                 D2D1::Point2F(cx, static_cast<float>(vd_height)),
                 res.solid_brush.Get(), 1.f, res.crosshair_style.Get());
    rt->DrawLine(D2D1::Point2F(0.f, cy),
                 D2D1::Point2F(static_cast<float>(vd_width), cy), res.solid_brush.Get(),
                 1.f, res.crosshair_style.Get());
}

void Draw_coord_tooltip(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                        core::PointPx cursor,
                        std::span<const core::RectPx> monitor_rects_client,
                        int vd_width, int vd_height) {
    if (!res.text_dim) {
        return;
    }
    std::wstring const coord_str =
        std::to_wstring(cursor.x) + L" x " + std::to_wstring(cursor.y);

    float tw = 0.f, th = 0.f;
    if (!Measure_text(res, res.text_dim.Get(), coord_str, tw, th)) {
        return;
    }

    constexpr float k_pad = 4.f;
    constexpr float k_margin = 4.f;
    float const box_w = tw + 2.f * k_margin;
    float const box_h = th + 2.f * k_margin;

    // Monitor bounds for tooltip placement.
    float mon_l = 0.f, mon_t = 0.f;
    float mon_r = static_cast<float>(vd_width);
    float mon_b = static_cast<float>(vd_height);
    for (auto const &m : monitor_rects_client) {
        if (m.Contains(cursor)) {
            mon_l = static_cast<float>(m.left);
            mon_t = static_cast<float>(m.top);
            mon_r = static_cast<float>(m.right);
            mon_b = static_cast<float>(m.bottom);
            break;
        }
    }

    float const cx = static_cast<float>(cursor.x);
    float const cy = static_cast<float>(cursor.y);
    float tt_left = cx + k_pad;
    float tt_top = cy + k_pad;
    if (tt_left + box_w > mon_r) {
        tt_left = cx - k_pad - box_w;
    }
    if (tt_top + box_h > mon_b) {
        tt_top = cy - k_pad - box_h;
    }
    if (tt_left < mon_l) {
        tt_left = mon_l;
    }
    if (tt_top < mon_t) {
        tt_top = mon_t;
    }

    Draw_text_box(rt, res, res.text_dim.Get(), coord_str, tt_left, tt_top, box_w, box_h,
                  k_margin);
}

// ---------------------------------------------------------------------------
// Magnifier
// ---------------------------------------------------------------------------

constexpr int kMagnifierSize = 256;
constexpr int kMagnifierZoom = 8;
constexpr int kMagnifierSource = kMagnifierSize / kMagnifierZoom; // 32
constexpr int kMagnifierPadding = 8;
constexpr int kMagnifierCrosshairThickness = 8;
constexpr int kMagnifierCrosshairGap = 20;
constexpr int kMagnifierCrosshairMargin = 8;
constexpr float kMagnifierCrosshairAlphaF = 168.f / 255.f;

void Draw_magnifier(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                    core::PointPx cursor,
                    std::span<const core::RectPx> monitor_rects_client, int vd_width,
                    int vd_height) {
    if (!res.screenshot || !res.factory) {
        return;
    }

    int const cx = cursor.x;
    int const cy = cursor.y;
    int const src_x = cx - kMagnifierSource / 2;
    int const src_y = cy - kMagnifierSource / 2;
    int const src_right = src_x + kMagnifierSource + 1;
    int const src_bottom = src_y + kMagnifierSource + 1;

    // Clamped source rect.
    int const sample_l = std::max(src_x, 0);
    int const sample_t = std::max(src_y, 0);
    int const sample_r = std::min(src_right, vd_width);
    int const sample_b = std::min(src_bottom, vd_height);

    // Monitor bounds for placement.
    int mon_l = 0, mon_t = 0, mon_r = vd_width, mon_b = vd_height;
    for (auto const &m : monitor_rects_client) {
        if (m.Contains(cursor)) {
            mon_l = m.left;
            mon_t = m.top;
            mon_r = m.right;
            mon_b = m.bottom;
            break;
        }
    }

    // Quadrant placement.
    constexpr int half_zoom = kMagnifierZoom / 2;
    int mag_l = 0, mag_t = 0;
    struct {
        int dx, dy;
    } constexpr quadrants[] = {
        {+kMagnifierPadding, +kMagnifierPadding},
        {-kMagnifierPadding - kMagnifierSize, +kMagnifierPadding},
        {+kMagnifierPadding, -kMagnifierPadding - kMagnifierSize},
        {-kMagnifierPadding - kMagnifierSize, -kMagnifierPadding - kMagnifierSize},
    };
    bool placed = false;
    for (auto const &q : quadrants) {
        int const ml = cx + q.dx;
        int const mt = cy + q.dy;
        if (ml >= mon_l && mt >= mon_t && ml + kMagnifierSize <= mon_r &&
            mt + kMagnifierSize <= mon_b) {
            mag_l = ml;
            mag_t = mt;
            placed = true;
            break;
        }
    }
    if (!placed) {
        int const max_l = std::max(mon_l, mon_r - kMagnifierSize);
        int const max_t = std::max(mon_t, mon_b - kMagnifierSize);
        mag_l = std::clamp(cx + kMagnifierPadding, mon_l, max_l);
        mag_t = std::clamp(cy + kMagnifierPadding, mon_t, max_t);
    }

    float const mag_lf = static_cast<float>(mag_l);
    float const mag_tf = static_cast<float>(mag_t);
    float const mag_sz = static_cast<float>(kMagnifierSize);
    float const radius = mag_sz / 2.f;
    D2D1_POINT_2F const center = D2D1::Point2F(mag_lf + radius, mag_tf + radius);

    // Build ellipse geometry for clipping.
    Microsoft::WRL::ComPtr<ID2D1EllipseGeometry> ellipse_geo;
    if (FAILED(res.factory->CreateEllipseGeometry(D2D1::Ellipse(center, radius, radius),
                                                  ellipse_geo.GetAddressOf()))) {
        return;
    }

    // Push clip layer.
    Microsoft::WRL::ComPtr<ID2D1Layer> layer;
    if (FAILED(rt->CreateLayer(nullptr, layer.GetAddressOf()))) {
        return;
    }
    D2D1_LAYER_PARAMETERS layer_params =
        D2D1::LayerParameters(D2D1::InfiniteRect(), ellipse_geo.Get());
    rt->PushLayer(layer_params, layer.Get());

    // Checkerboard background (covers the full magnifier area, tiled from source).
    {
        constexpr int tile = 16; // kMagnifierCheckerTile
        int const tile_sx0 = src_x & ~1;
        int const tile_sy0 = src_y & ~1;
        int const mx0 = (tile_sx0 - src_x) * kMagnifierZoom - half_zoom;
        int const my0 = (tile_sy0 - src_y) * kMagnifierZoom - half_zoom;
        int const base_tx = tile_sx0 / 2;
        int const base_ty = tile_sy0 / 2;
        for (int ty = 0;; ++ty) {
            int const my = my0 + ty * tile;
            if (my >= kMagnifierSize) {
                break;
            }
            int const cy0 = std::max(0, my);
            int const cy1 = std::min(kMagnifierSize, my + tile);
            if (cy1 <= cy0) {
                continue;
            }
            for (int tx = 0;; ++tx) {
                int const mx = mx0 + tx * tile;
                if (mx >= kMagnifierSize) {
                    break;
                }
                int const cx0 = std::max(0, mx);
                int const cx1 = std::min(kMagnifierSize, mx + tile);
                if (cx1 <= cx0) {
                    continue;
                }
                bool const dark = ((base_tx + tx + base_ty + ty) & 1) != 0;
                res.solid_brush->SetColor(dark ? kMagnifierCheckerDark
                                               : kMagnifierCheckerLight);
                rt->FillRectangle(D2D1::RectF(mag_lf + static_cast<float>(cx0),
                                              mag_tf + static_cast<float>(cy0),
                                              mag_lf + static_cast<float>(cx1),
                                              mag_tf + static_cast<float>(cy1)),
                                  res.solid_brush.Get());
            }
        }
    }

    // Zoomed screenshot — only cover sampled area.
    if (sample_l < sample_r && sample_t < sample_b) {
        float const dst_l =
            mag_lf +
            static_cast<float>((sample_l - src_x) * kMagnifierZoom - half_zoom);
        float const dst_t =
            mag_tf +
            static_cast<float>((sample_t - src_y) * kMagnifierZoom - half_zoom);
        float const dst_w = static_cast<float>((sample_r - sample_l) * kMagnifierZoom);
        float const dst_h = static_cast<float>((sample_b - sample_t) * kMagnifierZoom);
        D2D1_RECT_F const dst_f =
            D2D1::RectF(dst_l, dst_t, dst_l + dst_w, dst_t + dst_h);
        D2D1_RECT_F const src_f =
            D2D1::RectF(static_cast<float>(sample_l), static_cast<float>(sample_t),
                        static_cast<float>(sample_r), static_cast<float>(sample_b));
        rt->DrawBitmap(res.screenshot.Get(), dst_f, 1.f,
                       D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, src_f);
    }

    // Crosshair arms (semi-transparent black body, white contour).
    {
        int const crosshair_left = (cx - src_x) * kMagnifierZoom - half_zoom;
        int const crosshair_top = (cy - src_y) * kMagnifierZoom - half_zoom;
        int const arm_left = std::clamp(crosshair_left, 0,
                                        kMagnifierSize - kMagnifierCrosshairThickness);
        int const arm_top =
            std::clamp(crosshair_top, 0, kMagnifierSize - kMagnifierCrosshairThickness);
        int const center_x = arm_left + kMagnifierCrosshairThickness / 2;
        int const center_y = arm_top + kMagnifierCrosshairThickness / 2;

        struct Arm {
            int x0, y0, x1, y1;
        };
        Arm const arms[4] = {
            {arm_left, kMagnifierCrosshairMargin,
             arm_left + kMagnifierCrosshairThickness,
             center_y - kMagnifierCrosshairGap},
            {arm_left, center_y + kMagnifierCrosshairGap,
             arm_left + kMagnifierCrosshairThickness,
             kMagnifierSize - kMagnifierCrosshairMargin},
            {kMagnifierCrosshairMargin, arm_top, center_x - kMagnifierCrosshairGap,
             arm_top + kMagnifierCrosshairThickness},
            {center_x + kMagnifierCrosshairGap, arm_top,
             kMagnifierSize - kMagnifierCrosshairMargin,
             arm_top + kMagnifierCrosshairThickness},
        };

        // White contour (1px halo around each arm).
        res.solid_brush->SetColor(
            D2D1::ColorF(1.f, 1.f, 1.f, kMagnifierCrosshairAlphaF));
        for (auto const &arm : arms) {
            if (arm.x1 <= arm.x0 || arm.y1 <= arm.y0) {
                continue;
            }
            rt->FillRectangle(D2D1::RectF(mag_lf + static_cast<float>(arm.x0 - 1),
                                          mag_tf + static_cast<float>(arm.y0 - 1),
                                          mag_lf + static_cast<float>(arm.x1 + 1),
                                          mag_tf + static_cast<float>(arm.y1 + 1)),
                              res.solid_brush.Get());
        }
        // Black arm body.
        res.solid_brush->SetColor(
            D2D1::ColorF(0.f, 0.f, 0.f, kMagnifierCrosshairAlphaF));
        for (auto const &arm : arms) {
            if (arm.x1 <= arm.x0 || arm.y1 <= arm.y0) {
                continue;
            }
            rt->FillRectangle(D2D1::RectF(mag_lf + static_cast<float>(arm.x0),
                                          mag_tf + static_cast<float>(arm.y0),
                                          mag_lf + static_cast<float>(arm.x1),
                                          mag_tf + static_cast<float>(arm.y1)),
                              res.solid_brush.Get());
        }
    }

    rt->PopLayer();

    // Magnifier border ellipse.
    res.solid_brush->SetColor(kBorderColor);
    rt->DrawEllipse(D2D1::Ellipse(center, radius - kMagnifierBorderInset,
                                  radius - kMagnifierBorderInset),
                    res.solid_brush.Get(), kMagnifierBorderStrokeWidth);
}

// ---------------------------------------------------------------------------
// Dimension labels
// ---------------------------------------------------------------------------

constexpr float kDimMarginF = 4.f;
constexpr float kDimGapF = 4.f;
constexpr float kCenterMinPad = 24.f;
// Transient center label: two margins on each side of the measured text (4× kDimMarginF
// total width).
constexpr int kTransientCenterLabelHorizontalMarginUnits = 4;

// Clamp a box position so it stays within [lo, hi].
void Clamp_box(float &left, float w, float lo, float hi) {
    if (left + w > hi) {
        left = hi - w;
    }
    if (left < lo) {
        left = lo;
    }
}

void Draw_dimension_labels(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                           core::RectPx sel, int vd_w, int vd_h, bool show_side,
                           bool show_center) {
    if (!rt || !res.text_dim || !res.text_center || sel.Is_empty()) {
        return;
    }
    if (!show_side && !show_center) {
        return;
    }

    int const sel_w = sel.Width();
    int const sel_h = sel.Height();
    std::wstring const w_str = std::to_wstring(sel_w);
    std::wstring const h_str = std::to_wstring(sel_h);
    std::wstring const c_str = w_str + L" x " + h_str;

    float const sel_l = static_cast<float>(sel.left);
    float const sel_t = static_cast<float>(sel.top);
    float const sel_r = static_cast<float>(sel.right);
    float const sel_b = static_cast<float>(sel.bottom);
    float const cx = (sel_l + sel_r) / 2.f;
    float const cy = (sel_t + sel_b) / 2.f;
    float const fw = static_cast<float>(vd_w);
    float const fh = static_cast<float>(vd_h);

    if (show_side) {
        float tw = 0.f, th = 0.f;
        float hw = 0.f, hh = 0.f;
        if (!Measure_text(res, res.text_dim.Get(), w_str, tw, th)) {
            return;
        }
        if (!Measure_text(res, res.text_dim.Get(), h_str, hw, hh)) {
            return;
        }

        // Width label: centered above/below selection.
        float wbox_w = tw + 2.f * kDimMarginF;
        float wbox_h = th + 2.f * kDimMarginF;
        float wbox_l = cx - wbox_w / 2.f;
        float wbox_t = sel_t - kDimGapF - wbox_h;
        if (wbox_t < 0.f) {
            wbox_t = sel_b + kDimGapF;
        }
        Clamp_box(wbox_l, wbox_w, 0.f, fw);
        Clamp_box(wbox_t, wbox_h, 0.f, fh);
        Draw_text_box(rt, res, res.text_dim.Get(), w_str, wbox_l, wbox_t, wbox_w,
                      wbox_h, kDimMarginF);

        // Height label: centered left/right of selection.
        float hbox_w = hw + 2.f * kDimMarginF;
        float hbox_h = hh + 2.f * kDimMarginF;
        float hbox_t = cy - hbox_h / 2.f;
        float hbox_l = sel_l - kDimGapF - hbox_w;
        if (hbox_l < 0.f) {
            hbox_l = sel_r + kDimGapF;
        }
        Clamp_box(hbox_l, hbox_w, 0.f, fw);
        Clamp_box(hbox_t, hbox_h, 0.f, fh);
        Draw_text_box(rt, res, res.text_dim.Get(), h_str, hbox_l, hbox_t, hbox_w,
                      hbox_h, kDimMarginF);
    }

    if (show_center) {
        float cw = 0.f, ch_f = 0.f;
        if (!Measure_text(res, res.text_center.Get(), c_str, cw, ch_f)) {
            return;
        }
        float cbox_w = cw + 2.f * kDimMarginF;
        float cbox_h = ch_f + 2.f * kDimMarginF;
        // Only draw if selection is large enough.
        if (static_cast<float>(sel_w) < cbox_w + 2.f * kCenterMinPad) {
            return;
        }
        if (static_cast<float>(sel_h) < cbox_h + 2.f * kCenterMinPad) {
            return;
        }
        float cbox_l = cx - cbox_w / 2.f;
        float cbox_t = cy - cbox_h / 2.f;
        Draw_text_box(rt, res, res.text_center.Get(), c_str, cbox_l, cbox_t, cbox_w,
                      cbox_h, kDimMarginF);
    }
}

void Draw_transient_center_label(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                                 core::RectPx sel, std::wstring_view text) {
    if (!rt || !res.text_center || sel.Is_empty() || text.empty()) {
        return;
    }
    // Use the widest possible label ("50") to fix the box width regardless of content.
    // Add 4× margin (two on each side) so the layout width has slack and never wraps.
    float fixed_w = 0.f, ch = 0.f;
    if (!Measure_text(res, res.text_center.Get(), L"50", fixed_w, ch)) {
        return;
    }
    float cbox_w =
        fixed_w +
        static_cast<float>(kTransientCenterLabelHorizontalMarginUnits) * kDimMarginF;
    float cbox_h = ch + 2.f * kDimMarginF;
    int const sel_w = sel.Width();
    int const sel_h = sel.Height();
    if (static_cast<float>(sel_w) < cbox_w + 2.f * kCenterMinPad) {
        return;
    }
    if (static_cast<float>(sel_h) < cbox_h + 2.f * kCenterMinPad) {
        return;
    }
    float const cx =
        (static_cast<float>(sel.left) + static_cast<float>(sel.right)) / 2.f;
    float const cy =
        (static_cast<float>(sel.top) + static_cast<float>(sel.bottom)) / 2.f;
    Draw_text_box(rt, res, res.text_center.Get(), text, cx - cbox_w / 2.f,
                  cy - cbox_h / 2.f, cbox_w, cbox_h, kDimMarginF);
}

// ---------------------------------------------------------------------------
// Selection border highlight (hovered handle edge)
// ---------------------------------------------------------------------------

void Draw_border_highlight(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                           core::RectPx sel, core::SelectionHandle handle) {
    if (sel.Is_empty()) {
        return;
    }
    core::RectPx const r = sel.Normalized();
    int const cw = std::min(core::kMaxCornerSizePx, r.Width() / 2);
    int const ch = std::min(core::kMaxCornerSizePx, r.Height() / 2);
    res.solid_brush->SetColor(kBorderColor);

    // Helper: draw a filled 3-pixel-wide line segment.
    // Each arm overhangs 1 px beyond the rect edge on its outer side (the -1.f / +2.f
    // offsets).
    constexpr float arm_outer = 1.f; // outer overhang = 1 px
    auto hline = [&](float x0, float x1, float y) {
        rt->FillRectangle(D2D1::RectF(x0, y - 1.f, x1, y + 2.f), res.solid_brush.Get());
    };
    auto vline = [&](float x, float y0, float y1) {
        rt->FillRectangle(D2D1::RectF(x - 1.f, y0, x + 2.f, y1), res.solid_brush.Get());
    };

    float const l = static_cast<float>(r.left);
    float const t = static_cast<float>(r.top);
    float const ri = static_cast<float>(r.right);
    float const b = static_cast<float>(r.bottom);
    float const fcw = static_cast<float>(cw);
    float const fch = static_cast<float>(ch);

    switch (handle) {
    case core::SelectionHandle::Top:
        hline(l + fcw, ri - fcw, t);
        break;
    case core::SelectionHandle::Bottom:
        hline(l + fcw, ri - fcw, b - 1.f);
        break;
    case core::SelectionHandle::Left:
        vline(l, t + fch, b - fch);
        break;
    case core::SelectionHandle::Right:
        vline(ri - 1.f, t + fch, b - fch);
        break;
    case core::SelectionHandle::TopLeft:
        hline(l - arm_outer, l + fcw, t);
        vline(l, t - arm_outer, t + fch);
        break;
    case core::SelectionHandle::TopRight:
        hline(ri - fcw, ri + arm_outer, t);
        vline(ri - 1.f, t - arm_outer, t + fch);
        break;
    case core::SelectionHandle::BottomLeft:
        hline(l - arm_outer, l + fcw, b - 1.f);
        vline(l, b - fch, b + arm_outer);
        break;
    case core::SelectionHandle::BottomRight:
        hline(ri - fcw, ri + arm_outer, b - 1.f);
        vline(ri - 1.f, b - fch, b + arm_outer);
        break;
    }
}

// ---------------------------------------------------------------------------
// Annotation handles
// ---------------------------------------------------------------------------

void Draw_square_outline_d2d(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                             core::RectPx bounds, COLORREF color) {
    if (bounds.Is_empty()) {
        return;
    }
    core::RectPx const r = bounds.Normalized();
    res.solid_brush->SetColor(Colorref_to_d2d(color));
    // 1px wide outline as a 1px hollow rect.
    D2D1_RECT_F const rf = Rect(r);
    rt->DrawRectangle(D2D1::RectF(rf.left + kHalfPixel, rf.top + kHalfPixel,
                                  rf.right - kHalfPixel, rf.bottom - kHalfPixel),
                      res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());
}

void Draw_endpoint_handle(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                          core::PointPx center) {
    int32_t const body = core::kAnnotationHandleBodySizePx;   // 10
    int32_t const halo = core::kAnnotationHandleHaloSizePx;   // 1
    int32_t const outer = core::kAnnotationHandleOuterSizePx; // 12
    auto square = [&](int32_t sz) -> core::RectPx {
        int32_t const h = sz / 2;
        return core::RectPx::From_ltrb(center.x - h, center.y - h, center.x - h + sz,
                                       center.y - h + sz);
    };
    Draw_square_outline_d2d(rt, res, square(outer), RGB(255, 255, 255));
    Draw_square_outline_d2d(rt, res, square(body), RGB(0, 0, 0));
    Draw_square_outline_d2d(rt, res, square(body - halo * 2), RGB(255, 255, 255));
}

void Draw_annotation_handles(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                             core::Annotation const *ann,
                             std::optional<core::RectPx> ann_bounds) {
    if (!ann) {
        return;
    }
    // Type-specific interactive handles.
    if (core::LineAnnotation const *const line =
            std::get_if<core::LineAnnotation>(&ann->data)) {
        Draw_endpoint_handle(rt, res, line->start);
        Draw_endpoint_handle(rt, res, line->end);
    } else if (core::FreehandStrokeAnnotation const *const fh =
                   std::get_if<core::FreehandStrokeAnnotation>(&ann->data);
               fh != nullptr &&
               fh->freehand_tip_shape == core::FreehandTipShape::Square &&
               fh->points.size() == 2) {
        Draw_endpoint_handle(rt, res, fh->points[0]);
        Draw_endpoint_handle(rt, res, fh->points[1]);
    } else if (core::RectangleAnnotation const *const rect =
                   std::get_if<core::RectangleAnnotation>(&ann->data)) {
        std::array<bool, 8> const visible =
            core::Visible_rectangle_resize_handles(rect->outer_bounds);
        for (size_t i = 0; i < visible.size(); ++i) {
            if (visible[i]) {
                Draw_endpoint_handle(
                    rt, res,
                    core::Rectangle_resize_handle_center(
                        rect->outer_bounds, static_cast<core::SelectionHandle>(i)));
            }
        }
    } else if (core::EllipseAnnotation const *const ellipse =
                   std::get_if<core::EllipseAnnotation>(&ann->data)) {
        std::array<bool, 8> const visible =
            core::Visible_rectangle_resize_handles(ellipse->outer_bounds);
        for (size_t i = 0; i < visible.size(); ++i) {
            if (visible[i]) {
                Draw_endpoint_handle(
                    rt, res,
                    core::Rectangle_resize_handle_center(
                        ellipse->outer_bounds, static_cast<core::SelectionHandle>(i)));
            }
        }
    }
    // L-bracket selection frame. ann_bounds is the tight axis-aligned bounding
    // box of the annotation's drawn geometry, not including interactive handles.
    if (core::Annotation_shows_corner_brackets(ann->Kind()) && ann_bounds.has_value() &&
        !ann_bounds->Is_empty()) {
        core::RectPx const r = ann_bounds->Normalized();
        int const cw = std::min(core::kMaxCornerSizePx, r.Width() / 2);
        int const ch = std::min(core::kMaxCornerSizePx, r.Height() / 2);
        res.solid_brush->SetColor(kBorderColor);
        float const l = static_cast<float>(r.left);
        float const t = static_cast<float>(r.top);
        float const ri = static_cast<float>(r.right - 1);
        float const b = static_cast<float>(r.bottom - 1);
        float const fcw = static_cast<float>(cw);
        float const fch = static_cast<float>(ch);

        // Top-left corner
        rt->DrawLine(D2D1::Point2F(l, t + fch), D2D1::Point2F(l, t),
                     res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());
        rt->DrawLine(D2D1::Point2F(l, t), D2D1::Point2F(l + fcw, t),
                     res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());
        // Top-right corner
        rt->DrawLine(D2D1::Point2F(ri - fcw, t), D2D1::Point2F(ri, t),
                     res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());
        rt->DrawLine(D2D1::Point2F(ri, t), D2D1::Point2F(ri, t + fch),
                     res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());
        // Bottom-left corner
        rt->DrawLine(D2D1::Point2F(l, b - fch), D2D1::Point2F(l, b),
                     res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());
        rt->DrawLine(D2D1::Point2F(l, b), D2D1::Point2F(l + fcw, b),
                     res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());
        // Bottom-right corner
        rt->DrawLine(D2D1::Point2F(ri - fcw, b), D2D1::Point2F(ri, b),
                     res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());
        rt->DrawLine(D2D1::Point2F(ri, b), D2D1::Point2F(ri, b - fch),
                     res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());
    }
}

// ---------------------------------------------------------------------------
// Cursor preview (clipped to selection)
// ---------------------------------------------------------------------------

void Draw_brush_cursor_preview(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                               core::PointPx cursor, int32_t width_px) {
    float const inner_d =
        static_cast<float>(std::max<int32_t>(core::StrokeStyle::kMinWidthPx, width_px));
    constexpr float white_w = 3.f;
    constexpr float black_w = 1.f;
    float const preview_d = inner_d + white_w;
    float const cx = static_cast<float>(cursor.x);
    float const cy = static_cast<float>(cursor.y);
    D2D1_ELLIPSE const ell =
        D2D1::Ellipse(D2D1::Point2F(cx, cy), preview_d / 2.f, preview_d / 2.f);
    res.solid_brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f));
    rt->DrawEllipse(ell, res.solid_brush.Get(), white_w, res.flat_cap_style.Get());
    res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
    rt->DrawEllipse(ell, res.solid_brush.Get(), black_w, res.flat_cap_style.Get());
}

// Minimal IDWriteTextRenderer that collects glyph run outlines as
// ID2D1TransformedGeometry objects (y-flipped, at layout origin 0,0).
// Stack-lifetime only — AddRef/Release are no-ops.
class GlyphOutlineRenderer final : public IDWriteTextRenderer {
  public:
    GlyphOutlineRenderer(
        ID2D1Factory *factory,
        std::vector<Microsoft::WRL::ComPtr<ID2D1TransformedGeometry>> &out)
        : factory_(factory), geometries_(out) {}
    GlyphOutlineRenderer(GlyphOutlineRenderer const &) = delete;
    GlyphOutlineRenderer(GlyphOutlineRenderer &&) = delete;
    GlyphOutlineRenderer &operator=(GlyphOutlineRenderer const &) = delete;
    GlyphOutlineRenderer &operator=(GlyphOutlineRenderer &&) = delete;

    // IUnknown (stack lifetime — no ref counting)
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return 1; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) noexcept override {
        return E_NOINTERFACE;
    }

    // IDWritePixelSnapping
    HRESULT STDMETHODCALLTYPE
    IsPixelSnappingDisabled(void *, BOOL *disabled) noexcept override {
        *disabled = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void *,
                                                  DWRITE_MATRIX *t) noexcept override {
        *t = {1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *, float *ppd) noexcept override {
        *ppd = 1.f;
        return S_OK;
    }

    // IDWriteTextRenderer
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void *, float bx, float by,
                                           DWRITE_MEASURING_MODE,
                                           DWRITE_GLYPH_RUN const *run,
                                           DWRITE_GLYPH_RUN_DESCRIPTION const *,
                                           IUnknown *) noexcept override {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf()))) {
            return S_OK;
        }
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(path->Open(sink.GetAddressOf()))) {
            return S_OK;
        }
        (void)run->fontFace->GetGlyphRunOutline(
            run->fontEmSize, run->glyphIndices, run->glyphAdvances, run->glyphOffsets,
            run->glyphCount, run->isSideways, run->bidiLevel % 2 != 0, sink.Get());
        sink->Close();

        // Glyph outlines are already in D2D's y-down coordinate system (DirectWrite
        // uses the same orientation). Translate to the baseline origin.
        D2D1::Matrix3x2F const xform = D2D1::Matrix3x2F::Translation(bx, by);
        Microsoft::WRL::ComPtr<ID2D1TransformedGeometry> tgeom;
        if (FAILED(factory_->CreateTransformedGeometry(path.Get(), xform,
                                                       tgeom.GetAddressOf()))) {
            return S_OK;
        }
        geometries_.push_back(std::move(tgeom));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void *, float, float,
                                            DWRITE_UNDERLINE const *,
                                            IUnknown *) noexcept override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void *, float, float,
                                                DWRITE_STRIKETHROUGH const *,
                                                IUnknown *) noexcept override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void *, float, float,
                                               IDWriteInlineObject *, BOOL, BOOL,
                                               IUnknown *) noexcept override {
        return S_OK;
    }

  private:
    ID2D1Factory *factory_;
    std::vector<Microsoft::WRL::ComPtr<ID2D1TransformedGeometry>> &geometries_;
};

void Draw_text_cursor_preview(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                              core::PointPx cursor,
                              core::TextAnnotationBaseStyle const &style,
                              std::array<std::wstring_view, 4> const &font_families) {
    if (res.dwrite_factory == nullptr || res.factory == nullptr) {
        return;
    }

    // Rebuild the cached glyph outline if font or size changed.
    bool const cache_valid =
        res.text_cursor_preview_cache.has_value() &&
        res.text_cursor_preview_cache->font_choice == style.font_choice &&
        res.text_cursor_preview_cache->point_size == style.point_size;
    if (!cache_valid) {
        std::wstring_view const family =
            Resolve_text_font_family(style.font_choice, font_families);
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format = Create_text_format(
            res.dwrite_factory.Get(), family, static_cast<float>(style.point_size));
        if (format == nullptr) {
            return;
        }
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout =
            Create_text_layout(res.dwrite_factory.Get(), format.Get(), L"A");
        if (layout == nullptr) {
            return;
        }
        // Offset by -baseline so the cached geometry has its baseline at y=0.
        // At draw time the cursor translation will then place the baseline at the
        // cursor tip (rather than the top-left of the layout box).
        DWRITE_LINE_METRICS line_metrics{};
        UINT32 line_count = 1;
        (void)layout->GetLineMetrics(&line_metrics, 1, &line_count);
        float const baseline_offset = -(line_count > 0 ? line_metrics.baseline : 0.f);

        D2DOverlayResources::TextCursorPreviewCache cache;
        cache.font_choice = style.font_choice;
        cache.point_size = style.point_size;
        GlyphOutlineRenderer renderer(res.factory.Get(), cache.geometries);
        (void)layout->Draw(nullptr, &renderer, 0.f, baseline_offset);
        res.text_cursor_preview_cache = std::move(cache);
    }

    if (res.text_cursor_preview_cache->geometries.empty()) {
        return;
    }

    // Translate to the cursor position for this frame, with a small visual offset:
    // right a few pixels so the cursor tip sits to the left of the letter, and
    // up a few pixels so the baseline aligns with the visible bottom of the cursor
    // arrow rather than the hotspot (which is at the tip, above the arrow tail).
    // These constants must match kTextPreviewXOffsetPx/kTextPreviewYOffsetPx in
    // annotation_controller.cpp so preview and placement stay in sync.
    constexpr float x_offset = 11.f;
    constexpr float y_offset = 10.f;
    D2D1_MATRIX_3X2_F old_transform{};
    rt->GetTransform(&old_transform);
    D2D1::Matrix3x2F const translation =
        D2D1::Matrix3x2F::Translation(static_cast<float>(cursor.x) + x_offset,
                                      static_cast<float>(cursor.y) + y_offset);
    D2D1::Matrix3x2F combined;
    D2D1::Matrix3x2F::ReinterpretBaseType(&combined)->SetProduct(
        *D2D1::Matrix3x2F::ReinterpretBaseType(&old_transform),
        *D2D1::Matrix3x2F::ReinterpretBaseType(&translation));
    rt->SetTransform(combined);

    constexpr float white_w = 3.f;
    constexpr float black_w = 1.f;
    for (auto const &geom : res.text_cursor_preview_cache->geometries) {
        res.solid_brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f));
        rt->DrawGeometry(geom.Get(), res.solid_brush.Get(), white_w, nullptr);
        res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
        rt->DrawGeometry(geom.Get(), res.solid_brush.Get(), black_w, nullptr);
    }

    rt->SetTransform(old_transform);
}

void Draw_arrow_cursor_preview(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                               core::PointPx cursor, int32_t width_px) {
    float const w =
        static_cast<float>(std::max<int32_t>(core::StrokeStyle::kMinWidthPx, width_px));

    // Stabilized arrowhead geometry: the shape that obtains when the line is long
    // enough that head_length no longer clamps to line length.
    float const head_length =
        kArrowBaseLength + w * (kArrowLengthPerStroke - kArrowOverlapPerStroke);
    float const head_half = (kArrowBaseWidth + w * kArrowWidthPerStroke) / 2.f;
    float const stub_length = head_length * 0.25f;

    float const cx = static_cast<float>(cursor.x);
    float const cy = static_cast<float>(cursor.y);

    // Pointing right. Tip at (cx, cy) so the arrowhead points at the cursor.
    float const tip_x = cx;
    float const base_x = tip_x - head_length;
    float const stub_start_x = base_x - stub_length;

    // Build a single closed contour tracing the full arrow silhouette (shaft + head),
    // so there is no internal edge where they meet.
    // Winding order (clockwise from top-left of shaft):
    //   shaft top-left → shaft top-right → head top corner →
    //   tip → head bottom corner → shaft bottom-right → shaft bottom-left → close
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> arrow_outline;
    if (FAILED(res.factory->CreatePathGeometry(arrow_outline.GetAddressOf()))) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(arrow_outline->Open(sink.GetAddressOf()))) {
        return;
    }
    float const shaft_half = w * kHalfPixel;
    sink->BeginFigure(D2D1::Point2F(stub_start_x, cy - shaft_half),
                      D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(base_x, cy - shaft_half));
    sink->AddLine(D2D1::Point2F(base_x, cy - head_half));
    sink->AddLine(D2D1::Point2F(tip_x, cy));
    sink->AddLine(D2D1::Point2F(base_x, cy + head_half));
    sink->AddLine(D2D1::Point2F(base_x, cy + shaft_half));
    sink->AddLine(D2D1::Point2F(stub_start_x, cy + shaft_half));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    constexpr float white_w = 3.f;
    constexpr float black_w = 1.f;

    // White pass first (thicker), then black on top (thinner).
    res.solid_brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f));
    rt->DrawGeometry(arrow_outline.Get(), res.solid_brush.Get(), white_w, nullptr);

    res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
    rt->DrawGeometry(arrow_outline.Get(), res.solid_brush.Get(), black_w, nullptr);
}

void Draw_square_cursor_preview(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                                core::PointPx cursor, int32_t width_px) {
    float const inner_sz =
        static_cast<float>(std::max<int32_t>(core::StrokeStyle::kMinWidthPx, width_px));
    constexpr float white_w = 3.f;
    constexpr float black_w = 1.f;
    float const half = inner_sz * kHalfPixel;
    float const cx = static_cast<float>(cursor.x);
    float const cy = static_cast<float>(cursor.y);

    D2D1_RECT_F const rf = D2D1::RectF(cx - half, cy - half, cx + half, cy + half);
    res.solid_brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f));
    rt->DrawRectangle(rf, res.solid_brush.Get(), white_w, res.flat_cap_style.Get());
    res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
    rt->DrawRectangle(rf, res.solid_brush.Get(), black_w, res.flat_cap_style.Get());
}

// ---------------------------------------------------------------------------
// Toolbar buttons + tooltip
// ---------------------------------------------------------------------------

void Draw_toolbar(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                  D2DPaintInput const &input) {
    if (input.toolbar_buttons.empty()) {
        return;
    }
    constexpr ButtonDrawContext btn_ctx{};
    for (size_t i = 0; i < input.toolbar_buttons.size(); ++i) {
        IOverlayButton *btn = input.toolbar_buttons[i];
        if (!btn) {
            continue;
        }
        ID2D1Bitmap *glyph = (i < input.toolbar_button_glyphs.size())
                                 ? input.toolbar_button_glyphs[i]
                                 : nullptr;
        btn->Draw_d2d(rt, res.solid_brush.Get(), glyph, btn_ctx);
    }

    // Tooltip above/below hovered button.
    if (!input.toolbar_tooltip_text.empty() &&
        input.hovered_toolbar_bounds.has_value() && res.text_dim) {
        float tw = 0.f, th = 0.f;
        if (!Measure_text(res, res.text_dim.Get(), input.toolbar_tooltip_text, tw,
                          th)) {
            return;
        }
        constexpr float tooltip_offset = 6.f;
        constexpr float tooltip_margin = 4.f;
        float const box_w = tw + 2.f * tooltip_margin;
        float const box_h = th + 2.f * tooltip_margin;
        core::RectPx const &anchor = *input.hovered_toolbar_bounds;
        float const anchor_cx =
            (static_cast<float>(anchor.left) + static_cast<float>(anchor.right)) / 2.f;
        float box_l = anchor_cx - box_w / 2.f;
        float box_t = static_cast<float>(anchor.top) - tooltip_offset - box_h;
        if (box_t < 0.f) {
            box_t = static_cast<float>(anchor.bottom) + tooltip_offset;
        }
        if (box_l < 0.f) {
            box_l = 0.f;
        }
        Draw_text_box(rt, res, res.text_dim.Get(), input.toolbar_tooltip_text, box_l,
                      box_t, box_w, box_h, tooltip_margin);
    }
}

// ---------------------------------------------------------------------------
// Color wheel
// ---------------------------------------------------------------------------

void Draw_color_wheel(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                      D2DPaintInput const &input) {
    if (!input.show_color_wheel || input.color_wheel_segment_count == 0) {
        return;
    }
    if (!res.factory) {
        return;
    }

    bool const has_hub = input.color_wheel_has_style_hub;
    bool const font_ring =
        has_hub && input.text_wheel_active_mode == core::TextWheelMode::Font;

    // Validate: non-hub wheels need a color array matching the segment count.
    if (!has_hub && input.color_wheel_colors.size() < input.color_wheel_segment_count) {
        return;
    }

    float const outer_r = static_cast<float>(core::kColorWheelOuterDiameterPx) / 2.f;
    float const inner_r = outer_r - core::kColorWheelWidthPx;
    float const hub_r = inner_r - core::kTextWheelHubRingGapPx;
    float const half_gap = core::kTextWheelHubHalfGapPx;
    float const cx = static_cast<float>(input.color_wheel_center_px.x);
    float const cy = static_cast<float>(input.color_wheel_center_px.y);

    constexpr double deg_to_rad = 3.14159265358979323846 / 180.0;
    constexpr double rad_to_deg = 180.0 / 3.14159265358979323846;

    auto arc_pt = [&](float r, float angle_deg) -> D2D1_POINT_2F {
        float const rad =
            static_cast<float>(static_cast<double>(angle_deg) * deg_to_rad);
        return D2D1::Point2F(cx + r * std::cosf(rad), cy + r * std::sinf(rad));
    };

    // Per-radius chord gap angles: asin(half_gap / r) gives the angular offset
    // at each radius such that the gap edge is a chord at distance half_gap_px
    // from the slot boundary — producing parallel segment edges.
    float const half_seg_gap_px = core::kColorWheelSegmentGapPx / 2.0f;
    float const outer_gap_deg = static_cast<float>(
        std::asin(static_cast<double>(half_seg_gap_px) / static_cast<double>(outer_r)) *
        rad_to_deg);
    float const inner_gap_deg = static_cast<float>(
        std::asin(static_cast<double>(half_seg_gap_px) / static_cast<double>(inner_r)) *
        rad_to_deg);
    float const half_slot_deg =
        (input.color_wheel_segment_count > 0)
            ? 180.f / static_cast<float>(input.color_wheel_segment_count)
            : 0.f;

    // --- Ring segments ---
    for (size_t seg = 0; seg < input.color_wheel_segment_count; ++seg) {
        core::ColorWheelSegmentGeometry const geo =
            core::Get_color_wheel_segment_geometry(seg,
                                                   input.color_wheel_segment_count);
        float const b_before = geo.center_angle_degrees - half_slot_deg;
        float const b_after = geo.center_angle_degrees + half_slot_deg;
        float const outer_sa = b_before + outer_gap_deg;
        float const outer_ea = b_after - outer_gap_deg;
        float const inner_sa = b_before + inner_gap_deg;
        float const inner_ea = b_after - inner_gap_deg;

        float const outer_sweep = outer_ea - outer_sa;
        float const inner_sweep = inner_ea - inner_sa;

        D2D1_POINT_2F const outer_start = arc_pt(outer_r, outer_sa);
        D2D1_POINT_2F const outer_end = arc_pt(outer_r, outer_ea);
        D2D1_POINT_2F const inner_end = arc_pt(inner_r, inner_ea);
        D2D1_POINT_2F const inner_start = arc_pt(inner_r, inner_sa);

        D2D1_ARC_SIZE const outer_arc_sz =
            (outer_sweep >= 180.f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
        D2D1_ARC_SIZE const inner_arc_sz =
            (inner_sweep >= 180.f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        if (FAILED(res.factory->CreatePathGeometry(path.GetAddressOf()))) {
            continue;
        }
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(path->Open(sink.GetAddressOf()))) {
            continue;
        }
        sink->BeginFigure(outer_start, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(D2D1::ArcSegment(outer_end, D2D1::SizeF(outer_r, outer_r), 0.f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, outer_arc_sz));
        sink->AddLine(inner_end);
        sink->AddArc(D2D1::ArcSegment(inner_start, D2D1::SizeF(inner_r, inner_r), 0.f,
                                      D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                      inner_arc_sz));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();

        bool const is_font_seg = font_ring;
        if (is_font_seg) {
            res.solid_brush->SetColor(kOverlayButtonFillColor);
        } else if (seg < input.color_wheel_colors.size()) {
            res.solid_brush->SetColor(Colorref_to_d2d(input.color_wheel_colors[seg]));
        } else {
            continue;
        }
        rt->FillGeometry(path.Get(), res.solid_brush.Get());

        // Segment border.
        res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
        rt->DrawGeometry(path.Get(), res.solid_brush.Get(),
                         core::kColorWheelSegmentBorderWidthPx,
                         res.round_cap_style.Get());

        if (is_font_seg && res.dwrite_factory != nullptr) {
            size_t const font_index = seg;
            if (font_index < input.color_wheel_font_families.size()) {
                std::wstring_view const family =
                    input.color_wheel_font_families[font_index].empty()
                        ? core::kDefaultTextFontFamilies[font_index]
                        : input.color_wheel_font_families[font_index];
                Microsoft::WRL::ComPtr<IDWriteTextFormat> preview_format =
                    Create_text_format(res.dwrite_factory.Get(), family,
                                       kColorWheelFontPreviewPointSize);
                if (preview_format) {
                    constexpr std::wstring_view preview_glyph = L"A";
                    float text_w = 0.f;
                    float text_h = 0.f;
                    if (Measure_text(res, preview_format.Get(), preview_glyph, text_w,
                                     text_h)) {
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> preview_layout =
                            Create_text_layout(
                                res.dwrite_factory.Get(), preview_format.Get(),
                                preview_glyph,
                                text_w + kColorWheelFontPreviewLayoutPaddingPx,
                                text_h + kColorWheelFontPreviewLayoutPaddingPx);
                        if (preview_layout) {
                            float const mid_r = (outer_r + inner_r) / 2.f;
                            D2D1_POINT_2F const label_center =
                                arc_pt(mid_r, geo.center_angle_degrees);
                            res.solid_brush->SetColor(kOverlayButtonOutlineColor);
                            rt->DrawTextLayout(
                                D2D1::Point2F(label_center.x - text_w / 2.f,
                                              label_center.y - text_h / 2.f),
                                preview_layout.Get(), res.solid_brush.Get());
                        }
                    }
                }
            }
        }
    }

    // --- Halos for selected/hovered ring segments ---
    auto draw_halo = [&](size_t seg, float inner_w, float outer_w) {
        if (seg >= input.color_wheel_segment_count) {
            return;
        }
        core::ColorWheelSegmentGeometry const geo =
            core::Get_color_wheel_segment_geometry(seg,
                                                   input.color_wheel_segment_count);
        float const sa = geo.center_angle_degrees - half_slot_deg + outer_gap_deg;
        float const ea = geo.center_angle_degrees + half_slot_deg - outer_gap_deg;

        float const inner_halo_r = outer_r +
                                   core::kColorWheelSegmentBorderWidthPx / 2.f +
                                   core::kColorWheelSelectionHaloGapPx + inner_w / 2.f;
        float const outer_halo_r = inner_halo_r + inner_w / 2.f + outer_w / 2.f;

        float const halo_sweep = ea - sa;
        D2D1_ARC_SIZE const arc_sz =
            (halo_sweep >= 180.f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

        // Black inner ring.
        {
            D2D1_POINT_2F const p0 = arc_pt(inner_halo_r, sa);
            D2D1_POINT_2F const p1 = arc_pt(inner_halo_r, ea);
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> arc_path;
            if (FAILED(res.factory->CreatePathGeometry(arc_path.GetAddressOf()))) {
                return;
            }
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> arc_sink;
            if (FAILED(arc_path->Open(arc_sink.GetAddressOf()))) {
                return;
            }
            arc_sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
            arc_sink->AddArc(
                D2D1::ArcSegment(p1, D2D1::SizeF(inner_halo_r, inner_halo_r), 0.f,
                                 D2D1_SWEEP_DIRECTION_CLOCKWISE, arc_sz));
            arc_sink->EndFigure(D2D1_FIGURE_END_OPEN);
            arc_sink->Close();
            res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
            rt->DrawGeometry(arc_path.Get(), res.solid_brush.Get(), inner_w,
                             res.round_cap_style.Get());
        }
        // Green outer ring.
        {
            D2D1_POINT_2F const p0 = arc_pt(outer_halo_r, sa);
            D2D1_POINT_2F const p1 = arc_pt(outer_halo_r, ea);
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> arc_path;
            if (FAILED(res.factory->CreatePathGeometry(arc_path.GetAddressOf()))) {
                return;
            }
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> arc_sink;
            if (FAILED(arc_path->Open(arc_sink.GetAddressOf()))) {
                return;
            }
            arc_sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
            arc_sink->AddArc(
                D2D1::ArcSegment(p1, D2D1::SizeF(outer_halo_r, outer_halo_r), 0.f,
                                 D2D1_SWEEP_DIRECTION_CLOCKWISE, arc_sz));
            arc_sink->EndFigure(D2D1_FIGURE_END_OPEN);
            arc_sink->Close();
            res.solid_brush->SetColor(kBorderColor);
            rt->DrawGeometry(arc_path.Get(), res.solid_brush.Get(), outer_w,
                             res.round_cap_style.Get());
        }
    };

    if (input.color_wheel_selected_segment.has_value()) {
        draw_halo(*input.color_wheel_selected_segment,
                  core::kColorWheelSelectionHaloInnerWidthPx,
                  core::kColorWheelSelectionHaloOuterWidthPx);
    }
    if (input.color_wheel_hovered_segment.has_value()) {
        draw_halo(*input.color_wheel_hovered_segment,
                  core::kColorWheelHoverHaloInnerWidthPx,
                  core::kColorWheelHoverHaloOuterWidthPx);
    }

    // --- Hub (text tool only) ---
    if (!has_hub) {
        return;
    }

    float const chord_h = std::sqrtf(hub_r * hub_r - half_gap * half_gap);

    // Build left (color) and right (font) hub path geometries.
    auto make_hub_path =
        [&](bool is_left) -> Microsoft::WRL::ComPtr<ID2D1PathGeometry> {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        if (FAILED(res.factory->CreatePathGeometry(path.GetAddressOf()))) {
            return nullptr;
        }
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(path->Open(sink.GetAddressOf()))) {
            return nullptr;
        }
        if (is_left) {
            // Left button: chord on right side, arc sweeps CCW through left side.
            D2D1_POINT_2F const p_top = D2D1::Point2F(cx - half_gap, cy - chord_h);
            D2D1_POINT_2F const p_bot = D2D1::Point2F(cx - half_gap, cy + chord_h);
            sink->BeginFigure(p_top, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddArc(D2D1::ArcSegment(p_bot, D2D1::SizeF(hub_r, hub_r), 0.f,
                                          D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                          D2D1_ARC_SIZE_SMALL));
            sink->AddLine(p_top);
        } else {
            // Right button: chord on left side, arc sweeps CW through right side.
            D2D1_POINT_2F const p_top = D2D1::Point2F(cx + half_gap, cy - chord_h);
            D2D1_POINT_2F const p_bot = D2D1::Point2F(cx + half_gap, cy + chord_h);
            sink->BeginFigure(p_top, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddArc(D2D1::ArcSegment(p_bot, D2D1::SizeF(hub_r, hub_r), 0.f,
                                          D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                          D2D1_ARC_SIZE_SMALL));
            sink->AddLine(p_top);
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        return path;
    };

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> left_path = make_hub_path(true);
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> right_path = make_hub_path(false);

    bool const left_active =
        (input.text_wheel_active_mode == core::TextWheelMode::Color);
    bool const right_active = !left_active;

    // Fill hub buttons.
    if (left_path) {
        res.solid_brush->SetColor(left_active ? kOverlayButtonOutlineColor
                                              : kOverlayButtonFillColor);
        rt->FillGeometry(left_path.Get(), res.solid_brush.Get());
    }
    if (right_path) {
        res.solid_brush->SetColor(right_active ? kOverlayButtonOutlineColor
                                               : kOverlayButtonFillColor);
        rt->FillGeometry(right_path.Get(), res.solid_brush.Get());
    }

    // Border: arc edge + chord edge.
    if (core::kTextWheelHubDrawBorder) {
        auto draw_hub_border = [&](bool is_left, bool is_active) {
            float const chord_x = is_left ? cx - half_gap : cx + half_gap;
            D2D1_COLOR_F const border_col =
                is_active ? kOverlayButtonFillColor : kOverlayButtonOutlineColor;
            D2D1_POINT_2F const p_top = D2D1::Point2F(chord_x, cy - chord_h);
            D2D1_POINT_2F const p_bot = D2D1::Point2F(chord_x, cy + chord_h);

            // Arc portion.
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> arc_path;
            if (FAILED(res.factory->CreatePathGeometry(arc_path.GetAddressOf()))) {
                return;
            }
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> arc_sink;
            if (FAILED(arc_path->Open(arc_sink.GetAddressOf()))) {
                return;
            }
            arc_sink->BeginFigure(p_top, D2D1_FIGURE_BEGIN_HOLLOW);
            arc_sink->AddArc(
                D2D1::ArcSegment(p_bot, D2D1::SizeF(hub_r, hub_r), 0.f,
                                 is_left ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
                                         : D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                 D2D1_ARC_SIZE_SMALL));
            arc_sink->EndFigure(D2D1_FIGURE_END_OPEN);
            arc_sink->Close();
            res.solid_brush->SetColor(border_col);
            rt->DrawGeometry(arc_path.Get(), res.solid_brush.Get(),
                             core::kColorWheelSegmentBorderWidthPx,
                             res.round_cap_style.Get());

            // Chord portion.
            rt->DrawLine(p_top, p_bot, res.solid_brush.Get(),
                         core::kColorWheelSegmentBorderWidthPx,
                         res.flat_cap_style.Get());
        };
        draw_hub_border(true, left_active);
        draw_hub_border(false, right_active);
    }

    // Left glyph: hue gradient rectangle.
    if (left_path && res.text_wheel_hue_brush) {
        float const glyph_cx = cx - (hub_r + half_gap) / 2.f;
        float const glyph_w = core::kTextWheelHubGlyphRectWidthPx;
        float const glyph_h = core::kTextWheelHubGlyphRectHeightPx;
        D2D1_RECT_F const glyph_rect =
            D2D1::RectF(glyph_cx - glyph_w / 2.f, cy - glyph_h / 2.f,
                        glyph_cx + glyph_w / 2.f, cy + glyph_h / 2.f);
        res.text_wheel_hue_brush->SetStartPoint(D2D1::Point2F(glyph_rect.left, cy));
        res.text_wheel_hue_brush->SetEndPoint(D2D1::Point2F(glyph_rect.right, cy));
        rt->FillRectangle(glyph_rect, res.text_wheel_hue_brush.Get());
        res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
        rt->DrawRectangle(glyph_rect, res.solid_brush.Get(), 1.f);
    }

    // Right glyph: "A" label in current font.
    if (right_path && res.dwrite_factory && !input.text_wheel_hub_font_family.empty()) {
        float const glyph_cx = cx + (hub_r + half_gap) / 2.f;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt = Create_text_format(
            res.dwrite_factory.Get(), input.text_wheel_hub_font_family,
            kColorWheelFontPreviewPointSize);
        if (fmt) {
            constexpr std::wstring_view label = L"A";
            float text_w = 0.f;
            float text_h = 0.f;
            if (Measure_text(res, fmt.Get(), label, text_w, text_h)) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> layout =
                    Create_text_layout(res.dwrite_factory.Get(), fmt.Get(), label,
                                       text_w + kColorWheelFontPreviewLayoutPaddingPx,
                                       text_h + kColorWheelFontPreviewLayoutPaddingPx);
                if (layout) {
                    res.solid_brush->SetColor(right_active
                                                  ? kOverlayButtonFillColor
                                                  : kOverlayButtonOutlineColor);
                    rt->DrawTextLayout(
                        D2D1::Point2F(glyph_cx - text_w / 2.f, cy - text_h / 2.f),
                        layout.Get(), res.solid_brush.Get());
                }
            }
        }
    }

    // Hover tint on hovered hub button.
    if (input.text_wheel_hovered_hub.has_value()) {
        bool const hovered_left =
            (*input.text_wheel_hovered_hub == core::TextWheelHubSide::Color);
        ID2D1PathGeometry *hovered_path =
            hovered_left ? left_path.Get() : right_path.Get();
        if (hovered_path) {
            D2D1_LAYER_PARAMETERS layer_params = D2D1::LayerParameters();
            layer_params.geometricMask = hovered_path;
            Microsoft::WRL::ComPtr<ID2D1Layer> layer;
            if (SUCCEEDED(rt->CreateLayer(nullptr, layer.GetAddressOf()))) {
                rt->PushLayer(layer_params, layer.Get());
                float const tint_alpha = 0.20f;
                res.solid_brush->SetColor(With_alpha(kBorderColor, tint_alpha));
                D2D1_RECT_F const hub_bounds =
                    D2D1::RectF(cx - hub_r, cy - hub_r, cx + hub_r, cy + hub_r);
                rt->FillRectangle(hub_bounds, res.solid_brush.Get());
                rt->PopLayer();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Draft stroke bitmap (incremental freehand accumulator)
// ---------------------------------------------------------------------------

void Clear_draft_stroke_surface(D2DOverlayResources &res) {
    if (res.draft_stroke_point_count == 0 && !res.draft_stroke_bitmap) {
        return;
    }

    res.draft_stroke_point_count = 0;
    res.draft_stroke_last_point = {};
    res.draft_stroke_bitmap.Reset();
    if (!res.draft_stroke_rt) {
        return;
    }

    res.draft_stroke_rt->BeginDraw();
    res.draft_stroke_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
    (void)res.draft_stroke_rt->EndDraw();
}

// Appends any new freehand segments to draft_stroke_rt since the last call.
// Round-tip: drawn with round cap stroke at configured opacity.
// Square-tip: drawn as convex hull fills at full opacity; opacity applied at blit time.
// When points is empty, the cached draft surface is cleared so the next gesture
// cannot resurrect stale pixels.
// Must be called BEFORE hwnd_rt->BeginDraw.
void Update_draft_stroke_bitmap(D2DOverlayResources &res,
                                std::span<const core::PointPx> points,
                                std::optional<core::StrokeStyle> const &style,
                                core::FreehandTipShape tip_shape) {
    if (points.empty() || !style.has_value()) {
        Clear_draft_stroke_surface(res);
        return;
    }
    if (!res.draft_stroke_rt) {
        return;
    }
    // Points shrank (e.g. after Straighten): reset so the surface is cleared and
    // redrawn from scratch on this call.
    if (points.size() < res.draft_stroke_point_count) {
        res.draft_stroke_point_count = 0;
    }
    // Same count: only skip if the endpoint hasn't moved (e.g. endpoint tracking
    // after Straighten changes points[1] without changing points.size()).
    if (points.size() == res.draft_stroke_point_count) {
        if (res.draft_stroke_point_count == 0 ||
            points.back() == res.draft_stroke_last_point) {
            return;
        }
        res.draft_stroke_point_count = 0; // force full clear + redraw
    }
    // Need at least 2 points to form a segment.
    if (points.size() < 2) {
        res.draft_stroke_point_count = points.size();
        return;
    }

    res.draft_stroke_rt->BeginDraw();

    if (res.draft_stroke_point_count == 0) {
        // New gesture: clear the surface before drawing.
        res.draft_stroke_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
    }

    // Draw only the segments that are new since the last update.
    size_t const from =
        res.draft_stroke_point_count > 0 ? res.draft_stroke_point_count - 1 : 0;

    if (tip_shape == core::FreehandTipShape::Round) {
        res.solid_brush->SetColor(Colorref_to_d2d(
            style->color, Alpha_from_opacity_percent(style->opacity_percent)));
        float const stroke_w = static_cast<float>(style->width_px);
        for (size_t i = from; i + 1 < points.size(); ++i) {
            res.draft_stroke_rt->DrawLine(Pt(points[i]), Pt(points[i + 1]),
                                          res.solid_brush.Get(), stroke_w,
                                          res.round_cap_style.Get());
        }
    } else {
        // Square tip: draw segment hulls at full opacity. Opacity is applied at blit
        // time so each covered pixel is blended exactly once.
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (res.factory &&
            SUCCEEDED(res.factory->CreatePathGeometry(path.GetAddressOf())) &&
            SUCCEEDED(path->Open(sink.GetAddressOf()))) {
            sink->SetFillMode(D2D1_FILL_MODE_WINDING);
            float const half_extent =
                static_cast<float>(std::max<int32_t>(core::StrokeStyle::kMinWidthPx,
                                                     style->width_px)) /
                2.f;
            for (size_t i = from; i + 1 < points.size(); ++i) {
                float const ax = static_cast<float>(points[i].x);
                float const ay = static_cast<float>(points[i].y);
                float const bx = static_cast<float>(points[i + 1].x);
                float const by = static_cast<float>(points[i + 1].y);
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
                for (size_t hi = 1; hi < hull.count; ++hi) {
                    sink->AddLine(hull.points[hi]);
                }
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
            sink->Close();
            res.solid_brush->SetColor(Colorref_to_d2d(style->color, 1.0f));
            res.draft_stroke_rt->FillGeometry(path.Get(), res.solid_brush.Get());
        }
    }

    HRESULT const hr = res.draft_stroke_rt->EndDraw();
    if (SUCCEEDED(hr)) {
        res.draft_stroke_point_count = points.size();
        res.draft_stroke_last_point = points.back();
        (void)res.draft_stroke_rt->GetBitmap(
            res.draft_stroke_bitmap.ReleaseAndGetAddressOf());
    }
}

// ---------------------------------------------------------------------------
// Live layer (drawn every frame)
// ---------------------------------------------------------------------------

void Draw_live_layer(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                     D2DPaintInput const &input, int vd_width, int vd_height) {
    // Draft annotation (while gesture is active).
    if (input.draft_text_annotation != nullptr) {
        Draw_draft_text(rt, res, input);
    } else if (input.draft_annotation != nullptr) {
        Draw_annotation(rt, res, *input.draft_annotation);
    } else if (!input.draft_freehand_points.empty() &&
               input.draft_freehand_style.has_value()) {
        if (res.draft_stroke_bitmap) {
            if (input.draft_freehand_tip_shape == core::FreehandTipShape::Square &&
                res.screenshot && res.multiply_effect) {
                // Multiply blend: result = screenshot * stroke (opacity baked in
                // stroke). Non-stroke pixels are transparent and leave the destination
                // unchanged.
                Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc;
                if (SUCCEEDED(rt->QueryInterface(IID_PPV_ARGS(&dc)))) {
                    // k1 = opacity (stroke is full-alpha; k1 scales the multiply
                    // result).
                    D2D1_VECTOR_4F const coeffs = {input.draft_freehand_blit_opacity,
                                                   0.0f, 0.0f, 0.0f};
                    (void)res.multiply_effect->SetValue(
                        D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, coeffs);
                    res.multiply_effect->SetInput(0, res.screenshot.Get());
                    res.multiply_effect->SetInput(1, res.draft_stroke_bitmap.Get());
                    dc->DrawImage(res.multiply_effect.Get());
                } else {
                    rt->DrawBitmap(res.draft_stroke_bitmap.Get(), nullptr,
                                   input.draft_freehand_blit_opacity);
                }
            } else {
                rt->DrawBitmap(res.draft_stroke_bitmap.Get(), nullptr,
                               input.draft_freehand_blit_opacity);
            }
        }
    }

    // Selection border: live_rect while dragging; final_selection otherwise.
    // Dashed during drag (rubber-band), solid once the selection is committed.
    bool const interacting = input.dragging || input.handle_dragging ||
                             input.move_dragging || input.modifier_preview;
    core::RectPx const disp_sel = interacting ? input.live_rect : input.final_selection;
    Draw_selection_border(rt, res, disp_sel, /*dashed=*/input.dragging);

    // Dimension labels (only while interacting).
    if (interacting && !disp_sel.Is_empty()) {
        Draw_dimension_labels(rt, res, disp_sel, vd_width, vd_height,
                              input.show_selection_size_side_labels,
                              input.show_selection_size_center_label &&
                                  !input.move_dragging);
    }

    // Transient center label (e.g. brush size overlay).
    if (!input.transient_center_label_text.empty() &&
        !input.final_selection.Is_empty()) {
        Draw_transient_center_label(rt, res, input.final_selection,
                                    input.transient_center_label_text);
    }

    // Crosshair + magnifier + coord tooltip (only before selection is made).
    bool const show_crosshair = input.final_selection.Is_empty() && !input.dragging &&
                                !input.handle_dragging && !input.move_dragging &&
                                !input.modifier_preview;
    if (show_crosshair) {
        core::PointPx const cur = input.cursor_client_px;
        if (cur.x >= 0 && cur.x < vd_width && cur.y >= 0 && cur.y < vd_height) {
            Draw_crosshair(rt, res, cur, vd_width, vd_height);
            Draw_magnifier(rt, res, cur, input.monitor_rects_client, vd_width,
                           vd_height);
            Draw_coord_tooltip(rt, res, cur, input.monitor_rects_client, vd_width,
                               vd_height);
        }
    }

    // Border highlight (hovered selection handle edge).
    if (input.highlight_handle.has_value() && !disp_sel.Is_empty()) {
        Draw_border_highlight(rt, res, disp_sel, *input.highlight_handle);
    }

    // Annotation handles (selected annotation).
    if (input.selected_annotation != nullptr) {
        Draw_annotation_handles(rt, res, input.selected_annotation,
                                input.selected_annotation_bounds);
    }

    // Cursor previews (clipped to final selection).
    if (input.brush_cursor_preview_width_px.has_value() ||
        input.square_cursor_preview_width_px.has_value() ||
        input.arrow_cursor_preview_width_px.has_value() ||
        input.text_cursor_preview_style.has_value()) {
        // Push clip to final selection.
        bool has_clip = false;
        Microsoft::WRL::ComPtr<ID2D1Layer> clip_layer;
        if (!input.final_selection.Is_empty() &&
            SUCCEEDED(rt->CreateLayer(nullptr, clip_layer.GetAddressOf()))) {
            D2D1_LAYER_PARAMETERS clip_params =
                D2D1::LayerParameters(Rect(input.final_selection));
            rt->PushLayer(clip_params, clip_layer.Get());
            has_clip = true;
        }

        if (input.brush_cursor_preview_width_px.has_value()) {
            Draw_brush_cursor_preview(rt, res, input.cursor_client_px,
                                      *input.brush_cursor_preview_width_px);
        }
        if (input.square_cursor_preview_width_px.has_value()) {
            Draw_square_cursor_preview(rt, res, input.cursor_client_px,
                                       *input.square_cursor_preview_width_px);
        }
        if (input.arrow_cursor_preview_width_px.has_value()) {
            Draw_arrow_cursor_preview(rt, res, input.cursor_client_px,
                                      *input.arrow_cursor_preview_width_px);
        }
        if (input.text_cursor_preview_style.has_value()) {
            Draw_text_cursor_preview(rt, res, input.cursor_client_px,
                                     *input.text_cursor_preview_style,
                                     input.color_wheel_font_families);
        }

        if (has_clip) {
            rt->PopLayer();
        }
    }

    // Toolbar buttons + tooltip.
    Draw_toolbar(rt, res, input);

    // Color wheel.
    Draw_color_wheel(rt, res, input);
}

// Draws all annotations to rt using multiply blend for square-tip freehand.
// rt must be in BeginDraw state on entry and will remain so on return.
// Temporarily EndDraw/BeginDraw's rt around each highlighter so draft_stroke_rt
// can be used as a scratch surface.
void Draw_annotations_to_rt(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                            std::span<const core::Annotation> annotations) {
    auto const is_highlighter = [](core::Annotation const &ann) -> bool {
        auto const *fh = std::get_if<core::FreehandStrokeAnnotation>(&ann.data);
        return fh && fh->freehand_tip_shape == core::FreehandTipShape::Square;
    };

    bool const can_multiply =
        res.screenshot && res.multiply_effect && res.draft_stroke_rt &&
        std::any_of(annotations.begin(), annotations.end(), is_highlighter);

    for (auto const &ann : annotations) {
        if (can_multiply && is_highlighter(ann)) {
            auto const &fh = *std::get_if<core::FreehandStrokeAnnotation>(&ann.data);

            // Suspend rt so draft_stroke_rt can be used as scratch.
            if (FAILED(rt->EndDraw())) {
                rt->BeginDraw();
                continue;
            }

            // Render the stroke at desired opacity into the scratch surface.
            res.draft_stroke_rt->BeginDraw();
            res.draft_stroke_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
            Draw_freehand_points(res.draft_stroke_rt.Get(), res, fh.points, fh.style,
                                 fh.freehand_tip_shape);
            bool drew_multiply = false;
            if (SUCCEEDED(res.draft_stroke_rt->EndDraw())) {
                Microsoft::WRL::ComPtr<ID2D1Bitmap> stroke_bmp;
                if (SUCCEEDED(res.draft_stroke_rt->GetBitmap(
                        stroke_bmp.ReleaseAndGetAddressOf()))) {
                    // k1 = 1: opacity is already baked into the stroke by
                    // Draw_freehand_points (single-pass FillGeometry, no accumulation).
                    D2D1_VECTOR_4F const coeffs = {1.0f, 0.0f, 0.0f, 0.0f};
                    (void)res.multiply_effect->SetValue(
                        D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, coeffs);
                    res.multiply_effect->SetInput(0, res.screenshot.Get());
                    res.multiply_effect->SetInput(1, stroke_bmp.Get());
                    rt->BeginDraw();
                    Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc;
                    if (SUCCEEDED(rt->QueryInterface(IID_PPV_ARGS(&dc)))) {
                        dc->DrawImage(res.multiply_effect.Get());
                        drew_multiply = true;
                    }
                }
            }
            if (!drew_multiply) {
                // Fallback: re-enter and draw with normal SOURCE_OVER.
                rt->BeginDraw();
                Draw_freehand_points(rt, res, fh.points, fh.style,
                                     fh.freehand_tip_shape);
            }
        } else {
            Draw_annotation(rt, res, ann);
        }
    }

    // If draft_stroke_rt was used as scratch, reset the incremental draw state and
    // clear the surface so the next gesture does not inherit stale pixels.
    if (can_multiply) {
        res.draft_stroke_point_count = 0;
        res.draft_stroke_bitmap.Reset();
        res.draft_stroke_rt->BeginDraw();
        res.draft_stroke_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
        (void)res.draft_stroke_rt->EndDraw();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Rebuild_annotations_bitmap(D2DOverlayResources &res,
                                std::span<const core::Annotation> annotations) {
    if (!res.annotations_rt) {
        return;
    }

    res.annotations_rt->BeginDraw();
    res.annotations_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
    Draw_annotations_to_rt(res.annotations_rt.Get(), res, annotations);

    HRESULT const hr = res.annotations_rt->EndDraw();
    if (SUCCEEDED(hr)) {
        (void)res.annotations_rt->GetBitmap(
            res.annotations_bitmap.ReleaseAndGetAddressOf());
        res.annotations_valid = true;
    }
}

void Rebuild_frozen_bitmap(D2DOverlayResources &res, core::RectPx selection,
                           int vd_width, int vd_height) {
    if (!res.frozen_rt || !res.screenshot || !res.annotations_bitmap) {
        return;
    }

    D2D1_RECT_F const full = D2D1::RectF(0.f, 0.f, static_cast<float>(vd_width),
                                         static_cast<float>(vd_height));

    res.frozen_rt->BeginDraw();
    res.frozen_rt->DrawBitmap(res.screenshot.Get());

    // Dim the entire capture.
    res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, kOverlayDimAlpha));
    res.frozen_rt->FillRectangle(full, res.solid_brush.Get());

    // Restore selection area to undimmed screenshot.
    if (!selection.Is_empty()) {
        D2D1_RECT_F const sel_f = Rect(selection);
        res.frozen_rt->DrawBitmap(res.screenshot.Get(), sel_f, 1.f,
                                  D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                                  sel_f);
    }

    // Composite committed annotations.
    res.frozen_rt->DrawBitmap(res.annotations_bitmap.Get());

    HRESULT const hr = res.frozen_rt->EndDraw();
    if (SUCCEEDED(hr)) {
        (void)res.frozen_rt->GetBitmap(res.frozen_bitmap.ReleaseAndGetAddressOf());
        res.frozen_valid = true;
    }
}

bool Paint_d2d_frame(D2DOverlayResources &res, D2DPaintInput const &input, int vd_width,
                     int vd_height, OverlayHelpOverlay *help_overlay) {
    if (!res.hwnd_rt) {
        return true;
    }

    Update_draft_stroke_bitmap(res, input.draft_freehand_points,
                               input.draft_freehand_style,
                               input.draft_freehand_tip_shape);

    // When an annotation edit interaction is active, the annotation under the cursor
    // changes every frame. Rebuild annotations_bitmap with the live state now, before
    // hwnd_rt->BeginDraw(), so the normal blit path gets the correct multiply-blend
    // rendering for highlighters without any mid-frame EndDraw on hwnd_rt.
    if (input.annotation_editing) {
        Rebuild_annotations_bitmap(res, input.annotations);
    }

    bool const is_steady_state = res.frozen_valid && !input.dragging &&
                                 !input.handle_dragging && !input.move_dragging &&
                                 !input.annotation_editing && !input.modifier_preview;

    res.hwnd_rt->BeginDraw();

    if (is_steady_state) {
        // Fastest path: one GPU blit of the frozen composite.
        if (res.frozen_bitmap) {
            res.hwnd_rt->DrawBitmap(res.frozen_bitmap.Get());
        }
    } else {
        // Dynamic path: rebuild per frame from screenshot + dim + selection +
        // annotations.
        D2D1_RECT_F const full = D2D1::RectF(0.f, 0.f, static_cast<float>(vd_width),
                                             static_cast<float>(vd_height));
        if (res.screenshot) {
            res.hwnd_rt->DrawBitmap(res.screenshot.Get());
        }
        res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, kOverlayDimAlpha));
        res.hwnd_rt->FillRectangle(full, res.solid_brush.Get());

        // Restore selection area: live_rect while dragging selection,
        // final_selection while editing an annotation (live_rect is empty then).
        core::RectPx const restore_rect =
            input.annotation_editing ? input.final_selection : input.live_rect;
        if (!restore_rect.Is_empty() && res.screenshot) {
            D2D1_RECT_F const sel_f = Rect(restore_rect);
            res.hwnd_rt->DrawBitmap(res.screenshot.Get(), sel_f, 1.f,
                                    D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                                    sel_f);
        }

        if (res.annotations_bitmap) {
            res.hwnd_rt->DrawBitmap(res.annotations_bitmap.Get());
        }
    }

    Draw_live_layer(res.hwnd_rt.Get(), res, input, vd_width, vd_height);

    if (help_overlay) {
        (void)help_overlay->Paint_d2d(res.hwnd_rt.Get(), res.dwrite_factory.Get(),
                                      res.solid_brush.Get());
    }

    HRESULT const hr = res.hwnd_rt->EndDraw();
    return hr != D2DERR_RECREATE_TARGET;
}

} // namespace greenflame
