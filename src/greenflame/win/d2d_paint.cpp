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

void Draw_annotation(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                     core::Annotation const &ann) {
    std::visit(core::Overloaded{
                   [&](core::FreehandStrokeAnnotation const &fh) {
                       Draw_freehand(rt, res, fh);
                   },
                   [&](core::LineAnnotation const &line) { Draw_line(rt, res, line); },
                   [&](core::RectangleAnnotation const &rect) {
                       Draw_rectangle(rt, res, rect);
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
    D2D1_RECT_F const inset =
        D2D1::RectF(rf.left + kHalfPixel, rf.top + kHalfPixel, rf.right - kHalfPixel,
                    rf.bottom - kHalfPixel);
    ID2D1StrokeStyle *style =
        dashed ? res.dashed_style.Get() : res.flat_cap_style.Get();
    rt->DrawRectangle(inset, res.solid_brush.Get(), 1.f, style);
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
                   float box_top, float box_w, float box_h, float margin) {
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
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    res.solid_brush->SetColor(kCoordTooltipText);
    rt->DrawTextLayout(D2D1::Point2F(box_left + margin, box_top + margin), layout.Get(),
                       res.solid_brush.Get());
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
    float cw = 0.f, ch = 0.f;
    if (!Measure_text(res, res.text_center.Get(), text, cw, ch)) {
        return;
    }
    float cbox_w = cw + 2.f * kDimMarginF;
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

void Draw_square_cursor_preview(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                                core::PointPx cursor, int32_t width_px,
                                std::optional<double> angle_radians) {
    float const inner_sz =
        static_cast<float>(std::max<int32_t>(core::StrokeStyle::kMinWidthPx, width_px));
    constexpr float white_w = 3.f;
    constexpr float black_w = 1.f;
    float const half = inner_sz * 0.5f;
    float const cx = static_cast<float>(cursor.x);
    float const cy = static_cast<float>(cursor.y);

    // Build a matrix for the optional rotation.
    D2D1_MATRIX_3X2_F old_transform{};
    rt->GetTransform(&old_transform);
    if (angle_radians.has_value()) {
        double const deg = *angle_radians * (180.0 / 3.14159265358979323846);
        D2D1_MATRIX_3X2_F rot =
            D2D1::Matrix3x2F::Rotation(static_cast<float>(deg), D2D1::Point2F(cx, cy));
        D2D1_MATRIX_3X2_F combined;
        D2D1::Matrix3x2F::ReinterpretBaseType(&combined)->SetProduct(
            *D2D1::Matrix3x2F::ReinterpretBaseType(&old_transform),
            *D2D1::Matrix3x2F::ReinterpretBaseType(&rot));
        rt->SetTransform(combined);
    }

    D2D1_RECT_F const rf = D2D1::RectF(cx - half, cy - half, cx + half, cy + half);
    res.solid_brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f));
    rt->DrawRectangle(rf, res.solid_brush.Get(), white_w, res.flat_cap_style.Get());
    res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
    rt->DrawRectangle(rf, res.solid_brush.Get(), black_w, res.flat_cap_style.Get());

    if (angle_radians.has_value()) {
        rt->SetTransform(old_transform);
    }
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
    if (!input.show_color_wheel || input.color_wheel_segment_count == 0 ||
        input.color_wheel_colors.size() < input.color_wheel_segment_count) {
        return;
    }
    if (!res.factory) {
        return;
    }

    float const outer_radius =
        static_cast<float>(core::kColorWheelOuterDiameterPx) / 2.f;
    float const inner_radius = outer_radius - core::kColorWheelWidthPx;
    float const cx = static_cast<float>(input.color_wheel_center_px.x);
    float const cy = static_cast<float>(input.color_wheel_center_px.y);

    constexpr double deg_to_rad = 3.14159265358979323846 / 180.0;

    auto arc_pt = [&](float r, float angle_deg) -> D2D1_POINT_2F {
        float const rad =
            static_cast<float>(static_cast<double>(angle_deg) * deg_to_rad);
        return D2D1::Point2F(cx + r * std::cosf(rad), cy + r * std::sinf(rad));
    };

    for (size_t seg = 0; seg < input.color_wheel_segment_count; ++seg) {
        core::ColorWheelSegmentGeometry const geo =
            core::Get_color_wheel_segment_geometry(seg,
                                                   input.color_wheel_segment_count);
        float const sa = geo.start_angle_degrees;
        float const ea = sa + geo.sweep_angle_degrees;

        D2D1_POINT_2F const outer_start = arc_pt(outer_radius, sa);
        D2D1_POINT_2F const outer_end = arc_pt(outer_radius, ea);
        D2D1_POINT_2F const inner_end = arc_pt(inner_radius, ea);
        D2D1_POINT_2F const inner_start = arc_pt(inner_radius, sa);

        D2D1_ARC_SIZE const arc_size = (geo.sweep_angle_degrees >= 180.f)
                                           ? D2D1_ARC_SIZE_LARGE
                                           : D2D1_ARC_SIZE_SMALL;

        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        if (FAILED(res.factory->CreatePathGeometry(path.GetAddressOf()))) {
            continue;
        }
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(path->Open(sink.GetAddressOf()))) {
            continue;
        }

        sink->BeginFigure(outer_start, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(D2D1::ArcSegment(outer_end,
                                      D2D1::SizeF(outer_radius, outer_radius), 0.f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, arc_size));
        sink->AddLine(inner_end);
        sink->AddArc(
            D2D1::ArcSegment(inner_start, D2D1::SizeF(inner_radius, inner_radius), 0.f,
                             D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, arc_size));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();

        // Fill with segment color.
        res.solid_brush->SetColor(Colorref_to_d2d(input.color_wheel_colors[seg]));
        rt->FillGeometry(path.Get(), res.solid_brush.Get());

        // Segment border.
        res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
        rt->DrawGeometry(path.Get(), res.solid_brush.Get(),
                         core::kColorWheelSegmentBorderWidthPx,
                         res.round_cap_style.Get());
    }

    // Halo for selected/hovered segment: arc strokes outside the wheel.
    auto draw_halo = [&](size_t seg, float inner_w, float outer_w) {
        if (seg >= input.color_wheel_segment_count) {
            return;
        }
        core::ColorWheelSegmentGeometry const geo =
            core::Get_color_wheel_segment_geometry(seg,
                                                   input.color_wheel_segment_count);
        float const sa = geo.start_angle_degrees;
        float const ea = sa + geo.sweep_angle_degrees;

        float const inner_halo_r = outer_radius +
                                   core::kColorWheelSegmentBorderWidthPx / 2.f +
                                   core::kColorWheelSelectionHaloGapPx + inner_w / 2.f;
        float const outer_halo_r = inner_halo_r + inner_w / 2.f + outer_w / 2.f;

        D2D1_ARC_SIZE const arc_sz = (geo.sweep_angle_degrees >= 180.f)
                                         ? D2D1_ARC_SIZE_LARGE
                                         : D2D1_ARC_SIZE_SMALL;

        // Black inner ring.
        {
            D2D1_POINT_2F const p0 = arc_pt(inner_halo_r, sa);
            D2D1_POINT_2F const p1 = arc_pt(inner_halo_r, ea);
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> arc_path;
            if (FAILED(res.factory->CreatePathGeometry(arc_path.GetAddressOf()))) {
                return;
            }
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(arc_path->Open(sink.GetAddressOf()))) {
                return;
            }
            sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(p1, D2D1::SizeF(inner_halo_r, inner_halo_r),
                                          0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, arc_sz));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
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
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(arc_path->Open(sink.GetAddressOf()))) {
                return;
            }
            sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(p1, D2D1::SizeF(outer_halo_r, outer_halo_r),
                                          0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, arc_sz));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
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
}

// ---------------------------------------------------------------------------
// Draft stroke bitmap (incremental freehand accumulator)
// ---------------------------------------------------------------------------

void Clear_draft_stroke_surface(D2DOverlayResources &res) {
    if (res.draft_stroke_point_count == 0 && !res.draft_stroke_bitmap) {
        return;
    }

    res.draft_stroke_point_count = 0;
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
    // Nothing new to draw.
    if (points.size() <= res.draft_stroke_point_count) {
        return;
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
    if (input.draft_annotation != nullptr) {
        Draw_annotation(rt, res, *input.draft_annotation);
    } else if (!input.draft_freehand_points.empty() &&
               input.draft_freehand_style.has_value()) {
        if (res.draft_stroke_bitmap) {
            rt->DrawBitmap(res.draft_stroke_bitmap.Get(), nullptr,
                           input.draft_freehand_blit_opacity);
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
        input.square_cursor_preview_width_px.has_value()) {
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
                                       *input.square_cursor_preview_width_px,
                                       input.square_cursor_preview_angle_radians);
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
    for (auto const &ann : annotations) {
        Draw_annotation(res.annotations_rt.Get(), res, ann);
    }
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

        if (input.annotation_editing) {
            // Draw all annotations from the live controller state so the annotation
            // being edited is shown at its current (in-progress) position/shape.
            for (auto const &ann : input.annotations) {
                Draw_annotation(res.hwnd_rt.Get(), res, ann);
            }
        } else if (res.annotations_bitmap) {
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
