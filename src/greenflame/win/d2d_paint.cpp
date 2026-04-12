#include "greenflame/win/d2d_paint.h"

#include "greenflame/win/d2d_annotation_draw.h"
#include "greenflame/win/d2d_draw_helpers.h"
#include "greenflame/win/d2d_overlay_resources.h"
#include "greenflame/win/overlay_button.h"
#include "greenflame/win/overlay_top_layer.h"
#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/selection_handles.h"
#include "greenflame_core/selection_wheel.h"
#include "win/ui_palette.h"

namespace greenflame {

namespace {

constexpr float kTextMeasureMaxExtent = 8192.f;
constexpr float kMagnifierBorderInset = 0.75f;
constexpr float kMagnifierBorderStrokeWidth = 1.5f;
constexpr float kOverlayDimAlpha = 0.5f;
constexpr float kCommittedSelectionBorderOutsideThicknessPx = 2.0f;
constexpr float kDraftTextSelectionAlpha = 0.7f;
constexpr float kDraftTextOverwriteCaretAlpha = 0.65f;
constexpr float kSelectionWheelFontPreviewPointSize = 18.f;
constexpr float kSelectionWheelFontPreviewLayoutPaddingPx = 4.f;
constexpr float kSelectionWheelHubContentDimBlendAlpha = 0.55f;
constexpr float kSelectionWheelHubInactiveOpacitySwipeOpacity =
    1.0f - kSelectionWheelHubContentDimBlendAlpha;
constexpr float kSelectionWheelHubOpacitySwipeLeadingAlpha = 0.9f;
constexpr float kSelectionWheelHubOpacitySwipeTrailingAlpha = 0.18f;
constexpr size_t kSelectionWheelHubOpacitySwipeBandCount = 6u;
constexpr std::wstring_view kOffscreenCaptureNoteText = L"Includes off-screen pixels";
constexpr D2D1_COLOR_F kSelectionWheelHubInactiveFill = Make_d2d_color(244, 246, 241);
constexpr D2D1_COLOR_F kSelectionWheelHubActiveFill = Make_d2d_color(220, 239, 216);
constexpr D2D1_COLOR_F kSelectionWheelHubHoverFill = Make_d2d_color(206, 231, 199);
constexpr D2D1_COLOR_F kSelectedAnnotationMarqueeBlack = Make_d2d_color(0, 0, 0);
constexpr D2D1_COLOR_F kSelectedAnnotationMarqueeGray = Make_d2d_color(187, 187, 187);
constexpr D2D1_COLOR_F kSelectedAnnotationMarqueeWhite = Make_d2d_color(255, 255, 255);
constexpr D2D1_COLOR_F kSpellSquiggleColor = Make_d2d_color(255, 0, 0);
constexpr float kSpellSquiggleAmplitudePx = 2.0f;
constexpr float kSpellSquiggleHalfPeriodPx = 2.0f;
constexpr float kSpellSquiggleStrokeWidthPx = 1.0f;
constexpr UINT32 kSpellSquiggleHitTestInitialCapacity = 8u;

void Draw_clipped_screenshot_rect(ID2D1RenderTarget *rt, ID2D1Bitmap *screenshot,
                                  core::RectPx restore_rect, int vd_width,
                                  int vd_height) {
    if (rt == nullptr || screenshot == nullptr || restore_rect.Is_empty()) {
        return;
    }

    std::optional<core::RectPx> const clipped = core::RectPx::Clip(
        restore_rect, core::RectPx::From_ltrb(0, 0, vd_width, vd_height));
    if (!clipped.has_value()) {
        return;
    }

    D2D1_RECT_F const visible_f = Rect(*clipped);
    rt->DrawBitmap(screenshot, visible_f, 1.f,
                   D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, visible_f);
}

void Draw_bitmap_rect(ID2D1RenderTarget *rt, ID2D1Bitmap *bitmap,
                      core::RectPx dest_rect, core::RectPx source_rect) {
    if (rt == nullptr || bitmap == nullptr || dest_rect.Is_empty() ||
        source_rect.Is_empty()) {
        return;
    }

    D2D1_SIZE_U const bitmap_size = bitmap->GetPixelSize();
    if (bitmap_size.width == 0 || bitmap_size.height == 0) {
        return;
    }

    core::RectPx const bitmap_bounds =
        core::RectPx::From_ltrb(0, 0, static_cast<int32_t>(bitmap_size.width),
                                static_cast<int32_t>(bitmap_size.height));
    std::optional<core::RectPx> const clipped_source =
        core::RectPx::Intersect(source_rect.Normalized(), bitmap_bounds);
    if (!clipped_source.has_value()) {
        return;
    }

    dest_rect.left += clipped_source->left - source_rect.left;
    dest_rect.top += clipped_source->top - source_rect.top;
    dest_rect.right -= source_rect.right - clipped_source->right;
    dest_rect.bottom -= source_rect.bottom - clipped_source->bottom;
    if (dest_rect.Is_empty()) {
        return;
    }
    source_rect = *clipped_source;

    rt->DrawBitmap(bitmap, Rect(dest_rect), 1.f,
                   D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, Rect(source_rect));
}

[[nodiscard]] constexpr D2D1_COLOR_F With_alpha(D2D1_COLOR_F color,
                                                float alpha) noexcept {
    return {color.r, color.g, color.b, alpha};
}

[[nodiscard]] constexpr D2D1_COLOR_F
Blend_colors(D2D1_COLOR_F base, D2D1_COLOR_F overlay, float overlay_alpha) noexcept {
    float const inverse_alpha = 1.0f - overlay_alpha;
    return {base.r * inverse_alpha + overlay.r * overlay_alpha,
            base.g * inverse_alpha + overlay.g * overlay_alpha,
            base.b * inverse_alpha + overlay.b * overlay_alpha,
            base.a * inverse_alpha + overlay.a * overlay_alpha};
}

enum class SelectedAnnotationMarqueeColor : uint8_t { Black, Gray, White };

[[nodiscard]] constexpr int32_t Positive_mod(int32_t value, int32_t modulus) noexcept {
    int32_t const remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

// Color at each position in the repeating 8-pixel pattern (Black–Gray–White–Gray).
constexpr std::array<SelectedAnnotationMarqueeColor,
                     kSelectedAnnotationMarqueePatternLengthPx>
    kMarqueePattern = {{
        SelectedAnnotationMarqueeColor::Black,
        SelectedAnnotationMarqueeColor::Black,
        SelectedAnnotationMarqueeColor::Black,
        SelectedAnnotationMarqueeColor::Gray,
        SelectedAnnotationMarqueeColor::White,
        SelectedAnnotationMarqueeColor::White,
        SelectedAnnotationMarqueeColor::White,
        SelectedAnnotationMarqueeColor::Gray,
    }};

// D2D color for each SelectedAnnotationMarqueeColor ordinal.
constexpr std::array<D2D1_COLOR_F, 3> kMarqueeColors = {{
    kSelectedAnnotationMarqueeBlack, // Black
    kSelectedAnnotationMarqueeGray,  // Gray
    kSelectedAnnotationMarqueeWhite, // White
}};

[[nodiscard]] std::wstring_view
Resolve_text_font_family(core::TextAnnotationBaseStyle const &style,
                         std::array<std::wstring_view, 4> const &families) noexcept {
    if (!style.font_family.empty()) {
        return style.font_family;
    }

    size_t const index = core::Text_font_choice_index(style.font_choice);
    if (families[index].empty()) {
        return core::kDefaultTextFontFamilies[index];
    }
    return families[index];
}

// ---------------------------------------------------------------------------
// Annotation drawing helpers
// ---------------------------------------------------------------------------

[[nodiscard]] D2DAnnotationDrawContext
Build_annotation_draw_context(D2DOverlayResources &res) noexcept {
    return D2DAnnotationDrawContext{
        .factory = res.factory.Get(),
        .solid_brush = res.solid_brush.Get(),
        .round_cap_style = res.round_cap_style.Get(),
        .flat_cap_style = res.flat_cap_style.Get(),
        .obfuscate_bitmaps = &res.obfuscate_bitmaps,
        .text_bitmaps = &res.text_bitmaps,
        .bubble_bitmaps = &res.bubble_bitmaps,
    };
}

void Draw_freehand_points(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                          std::span<const core::PointPx> points,
                          core::StrokeStyle style, core::FreehandTipShape tip_shape) {
    Draw_d2d_freehand_points(rt, Build_annotation_draw_context(res), points, style,
                             tip_shape);
}

void Draw_annotation(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                     core::Annotation const &ann) {
    Draw_d2d_annotation(rt, Build_annotation_draw_context(res), ann);
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
    if (dashed) {
        D2D1_RECT_F const rf = Rect(sel);
        float const l = rf.left + kHalfPixel;
        float const r = rf.right - kHalfPixel;
        float const t = rf.top + kHalfPixel;
        float const b = rf.bottom - kHalfPixel;
        // Four distinct lines (no marquee): horizontals both left-to-right,
        // verticals both top-to-bottom.
        rt->DrawLine(D2D1::Point2F(l, t), D2D1::Point2F(r, t), res.solid_brush.Get(),
                     1.f, res.dashed_style.Get());
        rt->DrawLine(D2D1::Point2F(l, b), D2D1::Point2F(r, b), res.solid_brush.Get(),
                     1.f, res.dashed_style.Get());
        rt->DrawLine(D2D1::Point2F(l, t), D2D1::Point2F(l, b), res.solid_brush.Get(),
                     1.f, res.dashed_style.Get());
        rt->DrawLine(D2D1::Point2F(r, t), D2D1::Point2F(r, b), res.solid_brush.Get(),
                     1.f, res.dashed_style.Get());
        return;
    }

    core::RectPx const bounds = sel.Normalized();
    float const l = static_cast<float>(bounds.left);
    float const t = static_cast<float>(bounds.top);
    float const r = static_cast<float>(bounds.right);
    float const b = static_cast<float>(bounds.bottom);
    // Committed selection chrome sits fully outside the clipped capture so it
    // remains visually distinct from the restored screenshot region.
    float const outside = kCommittedSelectionBorderOutsideThicknessPx;
    rt->FillRectangle(D2D1::RectF(l - outside, t - outside, r + outside, t),
                      res.solid_brush.Get());
    rt->FillRectangle(D2D1::RectF(l - outside, b, r + outside, b + outside),
                      res.solid_brush.Get());
    rt->FillRectangle(D2D1::RectF(l - outside, t, l, b), res.solid_brush.Get());
    rt->FillRectangle(D2D1::RectF(r, t, r + outside, b), res.solid_brush.Get());
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

    std::wstring const family_name(family);
    HRESULT const hr = factory->CreateTextFormat(
        family_name.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, point_size, L"",
        format.GetAddressOf());
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
        Resolve_text_font_family(annotation.base_style, font_families);
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
    if (Build_draft_text_layout(res, *annotation, input.selection_wheel_font_families,
                                format, layout, layout_data)) {
        res.solid_brush->SetColor(Colorref_to_d2d(annotation->base_style.color));
        rt->DrawTextLayout(Pt(annotation->origin), layout.Get(), res.solid_brush.Get());

        if (!input.draft_text_spell_errors.empty()) {
            float const origin_x = static_cast<float>(annotation->origin.x);
            float const origin_y = static_cast<float>(annotation->origin.y);
            std::vector<DWRITE_HIT_TEST_METRICS> metrics(
                kSpellSquiggleHitTestInitialCapacity);
            res.solid_brush->SetColor(kSpellSquiggleColor);
            for (core::SpellError const &error : input.draft_text_spell_errors) {
                if (error.length_utf16 <= 0) {
                    continue;
                }
                auto const err_start = static_cast<UINT32>(error.start_utf16);
                auto const err_length = static_cast<UINT32>(error.length_utf16);
                UINT32 actual_count = 0;
                HRESULT hr = layout->HitTestTextRange(
                    err_start, err_length, origin_x, origin_y, metrics.data(),
                    static_cast<UINT32>(metrics.size()), &actual_count);
                if (hr == E_NOT_SUFFICIENT_BUFFER) {
                    metrics.resize(actual_count);
                    hr = layout->HitTestTextRange(
                        err_start, err_length, origin_x, origin_y, metrics.data(),
                        static_cast<UINT32>(metrics.size()), &actual_count);
                }
                if (FAILED(hr)) {
                    continue;
                }
                for (UINT32 i = 0; i < actual_count; ++i) {
                    DWRITE_HIT_TEST_METRICS const &m = metrics[i];
                    float const y_base = m.top + m.height;
                    float const x_end = m.left + m.width;
                    float x = m.left;
                    bool going_up = true;
                    while (x < x_end) {
                        float const x_next =
                            std::min(x + kSpellSquiggleHalfPeriodPx, x_end);
                        float const y_from =
                            going_up ? y_base : y_base - kSpellSquiggleAmplitudePx;
                        float const y_to =
                            going_up ? y_base - kSpellSquiggleAmplitudePx : y_base;
                        rt->DrawLine(D2D1::Point2F(x, y_from),
                                     D2D1::Point2F(x_next, y_to), res.solid_brush.Get(),
                                     kSpellSquiggleStrokeWidthPx);
                        x = x_next;
                        going_up = !going_up;
                    }
                }
            }
        }
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
                           core::RectPx sel,
                           std::optional<core::SizePx> selection_size_override,
                           int vd_w, int vd_h, bool show_side, bool show_center,
                           bool show_offscreen_capture_note) {
    if (!rt || !res.text_dim || !res.text_center || sel.Is_empty()) {
        return;
    }
    if (!show_side && !show_center) {
        return;
    }

    int const label_w =
        selection_size_override.has_value() ? selection_size_override->w : sel.Width();
    int const label_h =
        selection_size_override.has_value() ? selection_size_override->h : sel.Height();
    int const visible_w = sel.Width();
    int const visible_h = sel.Height();
    std::wstring const w_str = std::to_wstring(label_w);
    std::wstring const h_str = std::to_wstring(label_h);
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
        float note_w = 0.f;
        float note_h = 0.f;
        if (show_offscreen_capture_note &&
            !Measure_text(res, res.text_dim.Get(), kOffscreenCaptureNoteText, note_w,
                          note_h)) {
            show_offscreen_capture_note = false;
        }

        float const cbox_w = std::max(cw, show_offscreen_capture_note ? note_w : 0.f) +
                             2.f * kDimMarginF;
        float const cbox_h =
            ch_f + (show_offscreen_capture_note ? kDimMarginF + note_h : 0.f) +
            2.f * kDimMarginF;
        // Only draw if selection is large enough.
        if (static_cast<float>(visible_w) < cbox_w + 2.f * kCenterMinPad) {
            return;
        }
        if (static_cast<float>(visible_h) < cbox_h + 2.f * kCenterMinPad) {
            return;
        }
        float cbox_l = cx - cbox_w / 2.f;
        float cbox_t = cy - cbox_h / 2.f;
        if (!show_offscreen_capture_note) {
            Draw_text_box(rt, res, res.text_center.Get(), c_str, cbox_l, cbox_t, cbox_w,
                          cbox_h, kDimMarginF);
            return;
        }

        D2D1_RECT_F const box_rect =
            D2D1::RectF(cbox_l, cbox_t, cbox_l + cbox_w, cbox_t + cbox_h);
        res.solid_brush->SetColor(With_alpha(kCoordTooltipBg, kCoordTooltipAlpha));
        rt->FillRectangle(box_rect, res.solid_brush.Get());

        res.solid_brush->SetColor(kCoordTooltipText);
        rt->DrawRectangle(D2D1::RectF(cbox_l + kHalfPixel, cbox_t + kHalfPixel,
                                      cbox_l + cbox_w - kHalfPixel,
                                      cbox_t + cbox_h - kHalfPixel),
                          res.solid_brush.Get(), 1.f, res.flat_cap_style.Get());

        float const inner_w = cbox_w - 2.f * kDimMarginF;
        float const size_text_top = cbox_t + kDimMarginF;
        float const note_text_top = size_text_top + ch_f + kDimMarginF;

        Microsoft::WRL::ComPtr<IDWriteTextLayout> size_layout;
        HRESULT const size_hr = res.dwrite_factory->CreateTextLayout(
            c_str.data(), static_cast<UINT32>(c_str.size()), res.text_center.Get(),
            inner_w, ch_f, size_layout.GetAddressOf());
        if (FAILED(size_hr) || !size_layout) {
            return;
        }
        size_layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        size_layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        Microsoft::WRL::ComPtr<IDWriteTextLayout> note_layout;
        HRESULT const note_hr = res.dwrite_factory->CreateTextLayout(
            kOffscreenCaptureNoteText.data(),
            static_cast<UINT32>(kOffscreenCaptureNoteText.size()), res.text_dim.Get(),
            inner_w, note_h, note_layout.GetAddressOf());
        if (FAILED(note_hr) || !note_layout) {
            return;
        }
        note_layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        note_layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        res.solid_brush->SetColor(kCoordTooltipText);
        rt->DrawTextLayout(D2D1::Point2F(cbox_l + kDimMarginF, size_text_top),
                           size_layout.Get(), res.solid_brush.Get());
        rt->DrawTextLayout(D2D1::Point2F(cbox_l + kDimMarginF, note_text_top),
                           note_layout.Get(), res.solid_brush.Get());
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

void Fill_horizontal_marquee_run(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                                 int32_t start_x, int32_t end_x, int32_t y,
                                 SelectedAnnotationMarqueeColor color) {
    if (start_x > end_x) {
        return;
    }

    res.solid_brush->SetColor(kMarqueeColors[static_cast<size_t>(color)]);
    rt->FillRectangle(D2D1::RectF(static_cast<float>(start_x), static_cast<float>(y),
                                  static_cast<float>(end_x + 1),
                                  static_cast<float>(y + 1)),
                      res.solid_brush.Get());
}

void Fill_vertical_marquee_run(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                               int32_t x, int32_t start_y, int32_t end_y,
                               SelectedAnnotationMarqueeColor color) {
    if (start_y > end_y) {
        return;
    }

    res.solid_brush->SetColor(kMarqueeColors[static_cast<size_t>(color)]);
    rt->FillRectangle(D2D1::RectF(static_cast<float>(x), static_cast<float>(start_y),
                                  static_cast<float>(x + 1),
                                  static_cast<float>(end_y + 1)),
                      res.solid_brush.Get());
}

void Draw_horizontal_marquee_edge(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                                  int32_t start_x, int32_t y, int32_t length,
                                  int32_t path_start, int32_t phase_px, bool reverse) {
    if (length <= 0) {
        return;
    }

    int32_t run_start = 0;
    while (run_start < length) {
        SelectedAnnotationMarqueeColor const color =
            kMarqueePattern[static_cast<size_t>(
                Positive_mod(path_start + run_start - phase_px,
                             kSelectedAnnotationMarqueePatternLengthPx))];
        int32_t run_end = run_start;
        while (run_end + 1 < length) {
            SelectedAnnotationMarqueeColor const next_color =
                kMarqueePattern[static_cast<size_t>(
                    Positive_mod(path_start + run_end + 1 - phase_px,
                                 kSelectedAnnotationMarqueePatternLengthPx))];
            if (next_color != color) {
                break;
            }
            ++run_end;
        }

        int32_t const edge_start_x = reverse ? start_x - run_end : start_x + run_start;
        int32_t const edge_end_x = reverse ? start_x - run_start : start_x + run_end;
        Fill_horizontal_marquee_run(rt, res, edge_start_x, edge_end_x, y, color);
        run_start = run_end + 1;
    }
}

void Draw_vertical_marquee_edge(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                                int32_t x, int32_t start_y, int32_t length,
                                int32_t path_start, int32_t phase_px, bool reverse) {
    if (length <= 0) {
        return;
    }

    int32_t run_start = 0;
    while (run_start < length) {
        SelectedAnnotationMarqueeColor const color =
            kMarqueePattern[static_cast<size_t>(
                Positive_mod(path_start + run_start - phase_px,
                             kSelectedAnnotationMarqueePatternLengthPx))];
        int32_t run_end = run_start;
        while (run_end + 1 < length) {
            SelectedAnnotationMarqueeColor const next_color =
                kMarqueePattern[static_cast<size_t>(
                    Positive_mod(path_start + run_end + 1 - phase_px,
                                 kSelectedAnnotationMarqueePatternLengthPx))];
            if (next_color != color) {
                break;
            }
            ++run_end;
        }

        int32_t const edge_start_y = reverse ? start_y - run_end : start_y + run_start;
        int32_t const edge_end_y = reverse ? start_y - run_start : start_y + run_end;
        Fill_vertical_marquee_run(rt, res, x, edge_start_y, edge_end_y, color);
        run_start = run_end + 1;
    }
}

void Draw_selected_annotation_marquee(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                                      std::optional<core::RectPx> selection_bounds,
                                      int32_t phase_px) {
    if (!selection_bounds.has_value()) {
        return;
    }

    core::RectPx const bounds = selection_bounds->Normalized();
    if (bounds.Is_empty()) {
        return;
    }

    int32_t const width = bounds.Width();
    int32_t const height = bounds.Height();
    if (width == 1) {
        Draw_vertical_marquee_edge(rt, res, bounds.left, bounds.top, height, 0,
                                   phase_px, false);
        return;
    }
    if (height == 1) {
        Draw_horizontal_marquee_edge(rt, res, bounds.left, bounds.top, width, 0,
                                     phase_px, false);
        return;
    }

    // Clockwise perimeter traversal with unique corner ownership:
    // top row owns both top corners, right edge owns bottom-right, bottom row owns
    // bottom-left, and left edge excludes both corners.
    int32_t const top_length = width;
    int32_t const right_length = height - 1;
    int32_t const bottom_length = width - 1;
    int32_t const left_length = height - 2;
    Draw_horizontal_marquee_edge(rt, res, bounds.left, bounds.top, top_length, 0,
                                 phase_px, false);
    Draw_vertical_marquee_edge(rt, res, bounds.right - 1, bounds.top + 1, right_length,
                               top_length, phase_px, false);
    Draw_horizontal_marquee_edge(rt, res, bounds.right - 2, bounds.bottom - 1,
                                 bottom_length, top_length + right_length, phase_px,
                                 true);
    Draw_vertical_marquee_edge(rt, res, bounds.left, bounds.bottom - 2, left_length,
                               top_length + right_length + bottom_length, phase_px,
                               true);
}

void Draw_annotation_handles(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                             core::Annotation const *ann) {
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
    } else if (core::ObfuscateAnnotation const *const obfuscate =
                   std::get_if<core::ObfuscateAnnotation>(&ann->data)) {
        std::array<bool, 8> const visible =
            core::Visible_rectangle_resize_handles(obfuscate->bounds);
        for (size_t i = 0; i < visible.size(); ++i) {
            if (visible[i]) {
                Draw_endpoint_handle(
                    rt, res,
                    core::Rectangle_resize_handle_center(
                        obfuscate->bounds, static_cast<core::SelectionHandle>(i)));
            }
        }
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
        res.text_cursor_preview_cache->font_family == style.font_family &&
        res.text_cursor_preview_cache->point_size == style.point_size;
    if (!cache_valid) {
        std::wstring_view const family = Resolve_text_font_family(style, font_families);
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
        cache.font_family = style.font_family;
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

void Draw_selection_wheel(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                          D2DPaintInput const &input) {
    if (!input.show_selection_wheel || input.selection_wheel_segment_count == 0) {
        return;
    }
    if (!res.factory) {
        return;
    }

    bool const has_hub = input.selection_wheel_has_style_hub;
    bool const has_highlighter_hub = input.selection_wheel_has_highlighter_hub;
    bool const font_ring =
        has_hub && input.text_wheel_active_mode == core::TextWheelMode::Font;
    bool const opacity_ring =
        has_highlighter_hub &&
        input.highlighter_wheel_active_mode == core::HighlighterWheelMode::Opacity;

    // Validate: non-hub wheels need a color array matching the segment count.
    if (!has_hub && !has_highlighter_hub &&
        input.selection_wheel_colors.size() < input.selection_wheel_segment_count) {
        return;
    }

    float const half_gap = core::kTextWheelHubHalfGapPx;
    float const cx = static_cast<float>(input.selection_wheel_center_px.x);
    float const cy = static_cast<float>(input.selection_wheel_center_px.y);

    constexpr double deg_to_rad = 3.14159265358979323846 / 180.0;

    auto arc_pt = [&](float r, float angle_deg) -> D2D1_POINT_2F {
        float const rad =
            static_cast<float>(static_cast<double>(angle_deg) * deg_to_rad);
        return D2D1::Point2F(cx + r * std::cosf(rad), cy + r * std::sinf(rad));
    };

    float const half_slot_deg =
        (input.selection_wheel_segment_count > 0)
            ? 180.f / static_cast<float>(input.selection_wheel_segment_count)
            : 0.f;

    float const ring_angle_offset = input.selection_wheel_ring_angle_offset;

    // Anchor checker brush to wheel center once; the transform is invariant across
    // all opacity ring segments drawn in this call.
    if (res.checker_brush) {
        res.checker_brush->SetTransform(D2D1::Matrix3x2F::Translation(cx, cy));
    }

    auto build_segment_path =
        [&](core::SelectionWheelSegmentGeometry const &geo, float segment_outer_r,
            float segment_inner_r,
            float half_gap_px = core::kSelectionWheelSegmentGapPx /
                                2.0f) -> Microsoft::WRL::ComPtr<ID2D1PathGeometry> {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        if (segment_outer_r <= segment_inner_r || segment_inner_r <= 0.0f) {
            return path;
        }

        float const outer_gap_deg =
            core::Selection_wheel_gap_half_angle_degrees(segment_outer_r, half_gap_px);
        float const inner_gap_deg =
            core::Selection_wheel_gap_half_angle_degrees(segment_inner_r, half_gap_px);
        float const b_before = geo.center_angle_degrees - half_slot_deg;
        float const b_after = geo.center_angle_degrees + half_slot_deg;
        float const outer_sa = b_before + outer_gap_deg;
        float const outer_ea = b_after - outer_gap_deg;
        float const inner_sa = b_before + inner_gap_deg;
        float const inner_ea = b_after - inner_gap_deg;

        float const outer_sweep = outer_ea - outer_sa;
        float const inner_sweep = inner_ea - inner_sa;
        if (outer_sweep <= 0.0f || inner_sweep <= 0.0f) {
            return path;
        }

        D2D1_POINT_2F const outer_start = arc_pt(segment_outer_r, outer_sa);
        D2D1_POINT_2F const outer_end = arc_pt(segment_outer_r, outer_ea);
        D2D1_POINT_2F const inner_end = arc_pt(segment_inner_r, inner_ea);
        D2D1_POINT_2F const inner_start = arc_pt(segment_inner_r, inner_sa);

        D2D1_ARC_SIZE const outer_arc_sz =
            (outer_sweep >= 180.f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
        D2D1_ARC_SIZE const inner_arc_sz =
            (inner_sweep >= 180.f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

        if (FAILED(res.factory->CreatePathGeometry(path.GetAddressOf()))) {
            return {};
        }
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(path->Open(sink.GetAddressOf()))) {
            return {};
        }
        sink->BeginFigure(outer_start, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(
            D2D1::ArcSegment(outer_end, D2D1::SizeF(segment_outer_r, segment_outer_r),
                             0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, outer_arc_sz));
        sink->AddLine(inner_end);
        sink->AddArc(D2D1::ArcSegment(
            inner_start, D2D1::SizeF(segment_inner_r, segment_inner_r), 0.f,
            D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, inner_arc_sz));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        return path;
    };

    auto build_segment_band = [&](ID2D1Geometry *outer_geometry,
                                  ID2D1Geometry *inner_geometry)
        -> Microsoft::WRL::ComPtr<ID2D1GeometryGroup> {
        Microsoft::WRL::ComPtr<ID2D1GeometryGroup> band;
        if (outer_geometry == nullptr || inner_geometry == nullptr) {
            return band;
        }
        ID2D1Geometry *geometries[] = {outer_geometry, inner_geometry};
        if (FAILED(res.factory->CreateGeometryGroup(
                D2D1_FILL_MODE_ALTERNATE, geometries, 2, band.GetAddressOf()))) {
            return {};
        }
        return band;
    };

    // --- Ring segments ---
    for (size_t seg = 0; seg < input.selection_wheel_segment_count; ++seg) {
        // Skip the phantom slot at the end of a clamped-nav ring.
        if (input.selection_wheel_clamp_nav &&
            seg + 1 == input.selection_wheel_segment_count) {
            continue;
        }
        core::SelectionWheelSegmentGeometry const geo =
            core::Get_selection_wheel_segment_geometry(
                seg, input.selection_wheel_segment_count, ring_angle_offset);
        core::SelectionWheelSegmentVisualMetrics const metrics =
            core::Get_selection_wheel_segment_visual_metrics(
                seg, input.selection_wheel_selected_segment,
                input.selection_wheel_hovered_segment);
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path =
            build_segment_path(geo, metrics.outer_radius_px, metrics.inner_radius_px);
        if (!path) {
            continue;
        }

        float const gray_inner_outer_r =
            metrics.outer_radius_px - core::kSelectionWheelRingOuterBorderWidthPx;
        float const gray_inner_inner_r =
            metrics.inner_radius_px + core::kSelectionWheelRingOuterBorderWidthPx;
        float const gray_half_gap_px = core::kSelectionWheelSegmentGapPx / 2.0f +
                                       core::kSelectionWheelRingOuterBorderWidthPx;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> gray_inner_path;
        if (gray_inner_outer_r > gray_inner_inner_r) {
            gray_inner_path = build_segment_path(geo, gray_inner_outer_r,
                                                 gray_inner_inner_r, gray_half_gap_px);
        }

        float const black_inner_outer_r =
            gray_inner_outer_r - core::kSelectionWheelRingInnerBorderWidthPx;
        float const black_inner_inner_r =
            gray_inner_inner_r + core::kSelectionWheelRingInnerBorderWidthPx;
        float const black_half_gap_px =
            gray_half_gap_px + core::kSelectionWheelRingInnerBorderWidthPx;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> black_inner_path;
        if (black_inner_outer_r > black_inner_inner_r) {
            black_inner_path = build_segment_path(
                geo, black_inner_outer_r, black_inner_inner_r, black_half_gap_px);
        }

        ID2D1Geometry *fill_geometry =
            black_inner_path  ? static_cast<ID2D1Geometry *>(black_inner_path.Get())
            : gray_inner_path ? static_cast<ID2D1Geometry *>(gray_inner_path.Get())
                              : static_cast<ID2D1Geometry *>(path.Get());

        bool const is_font_seg = font_ring;
        if (is_font_seg) {
            res.solid_brush->SetColor(kOverlayButtonFillColor);
            rt->FillGeometry(fill_geometry, res.solid_brush.Get());
        } else if (opacity_ring) {
            // Checker background (transform already set above), then color overlay.
            if (res.checker_brush) {
                rt->FillGeometry(fill_geometry, res.checker_brush.Get());
            }
            if (seg < core::kHighlighterOpacityPresets.size()) {
                res.solid_brush->SetColor(Colorref_to_d2d(
                    input.highlighter_wheel_current_color,
                    Alpha_from_opacity_percent(core::kHighlighterOpacityPresets[seg])));
                rt->FillGeometry(fill_geometry, res.solid_brush.Get());
            }
        } else if (has_highlighter_hub) {
            // Color mode: show fully opaque swatches so colors are clearly visible.
            if (seg < input.selection_wheel_colors.size()) {
                res.solid_brush->SetColor(
                    Colorref_to_d2d(input.selection_wheel_colors[seg], 1.0f));
                rt->FillGeometry(fill_geometry, res.solid_brush.Get());
            } else {
                continue;
            }
        } else if (seg < input.selection_wheel_colors.size()) {
            res.solid_brush->SetColor(
                Colorref_to_d2d(input.selection_wheel_colors[seg]));
            rt->FillGeometry(fill_geometry, res.solid_brush.Get());
        } else {
            continue;
        }

        if (gray_inner_path) {
            Microsoft::WRL::ComPtr<ID2D1GeometryGroup> gray_band =
                build_segment_band(path.Get(), gray_inner_path.Get());
            if (gray_band) {
                res.solid_brush->SetColor(
                    Colorref_to_d2d(core::kSelectionWheelRingOuterBorderColor, 1.0f));
                rt->FillGeometry(gray_band.Get(), res.solid_brush.Get());
            }
        }

        if (gray_inner_path && black_inner_path) {
            Microsoft::WRL::ComPtr<ID2D1GeometryGroup> black_band =
                build_segment_band(gray_inner_path.Get(), black_inner_path.Get());
            if (black_band) {
                res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
                rt->FillGeometry(black_band.Get(), res.solid_brush.Get());
            }
        }

        if (is_font_seg && res.dwrite_factory != nullptr) {
            size_t const font_index = seg;
            if (font_index < input.selection_wheel_font_families.size()) {
                std::wstring_view const family =
                    input.selection_wheel_font_families[font_index].empty()
                        ? core::kDefaultTextFontFamilies[font_index]
                        : input.selection_wheel_font_families[font_index];
                Microsoft::WRL::ComPtr<IDWriteTextFormat> preview_format =
                    Create_text_format(res.dwrite_factory.Get(), family,
                                       kSelectionWheelFontPreviewPointSize);
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
                                text_w + kSelectionWheelFontPreviewLayoutPaddingPx,
                                text_h + kSelectionWheelFontPreviewLayoutPaddingPx);
                        if (preview_layout) {
                            float const mid_r =
                                (metrics.outer_radius_px + metrics.inner_radius_px) /
                                2.f;
                            D2D1_POINT_2F const label_center =
                                arc_pt(mid_r, geo.center_angle_degrees);
                            res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
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

    // --- Hub (text tool / highlighter) ---
    if (!has_hub && !has_highlighter_hub) {
        return;
    }

    // Build left and right hub path geometries (shared by both hub types).
    auto make_hub_path = [&](bool is_left, float hub_radius,
                             float hub_half_gap = core::kTextWheelHubHalfGapPx)
        -> Microsoft::WRL::ComPtr<ID2D1PathGeometry> {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        if (hub_radius <= hub_half_gap) {
            return path;
        }
        float const chord_h =
            std::sqrtf(hub_radius * hub_radius - hub_half_gap * hub_half_gap);
        if (FAILED(res.factory->CreatePathGeometry(path.GetAddressOf()))) {
            return nullptr;
        }
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(path->Open(sink.GetAddressOf()))) {
            return nullptr;
        }
        if (is_left) {
            // Left button: chord on right side, arc sweeps CCW through left side.
            D2D1_POINT_2F const p_top = D2D1::Point2F(cx - hub_half_gap, cy - chord_h);
            D2D1_POINT_2F const p_bot = D2D1::Point2F(cx - hub_half_gap, cy + chord_h);
            sink->BeginFigure(p_top, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddArc(D2D1::ArcSegment(p_bot, D2D1::SizeF(hub_radius, hub_radius),
                                          0.f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                          D2D1_ARC_SIZE_SMALL));
            sink->AddLine(p_top);
        } else {
            // Right button: chord on left side, arc sweeps CW through right side.
            D2D1_POINT_2F const p_top = D2D1::Point2F(cx + hub_half_gap, cy - chord_h);
            D2D1_POINT_2F const p_bot = D2D1::Point2F(cx + hub_half_gap, cy + chord_h);
            sink->BeginFigure(p_top, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddArc(D2D1::ArcSegment(p_bot, D2D1::SizeF(hub_radius, hub_radius),
                                          0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                          D2D1_ARC_SIZE_SMALL));
            sink->AddLine(p_top);
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        return path;
    };

    struct HubPathBundle final {
        core::SelectionWheelHubVisualMetrics metrics = {};
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> outer_path;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> gray_inner_path;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> black_inner_path;
        float glyph_center_x = 0.0f;
    };

    auto hub_fill_color = [](core::SelectionWheelHubVisualState state) noexcept {
        switch (state) {
        case core::SelectionWheelHubVisualState::Normal:
            return kSelectionWheelHubInactiveFill;
        case core::SelectionWheelHubVisualState::Selected:
            return kSelectionWheelHubActiveFill;
        case core::SelectionWheelHubVisualState::Hovered:
            return kSelectionWheelHubHoverFill;
        }
        return kSelectionWheelHubInactiveFill;
    };

    auto hub_content_color = [&](core::SelectionWheelHubVisualState state,
                                 D2D1_COLOR_F active_color) noexcept {
        switch (state) {
        case core::SelectionWheelHubVisualState::Normal:
            return Blend_colors(active_color, hub_fill_color(state),
                                kSelectionWheelHubContentDimBlendAlpha);
        case core::SelectionWheelHubVisualState::Selected:
        case core::SelectionWheelHubVisualState::Hovered:
            return active_color;
        }
        return active_color;
    };

    auto hub_opacity_swipe_opacity =
        [](core::SelectionWheelHubVisualState state) noexcept {
            switch (state) {
            case core::SelectionWheelHubVisualState::Normal:
                return kSelectionWheelHubInactiveOpacitySwipeOpacity;
            case core::SelectionWheelHubVisualState::Selected:
            case core::SelectionWheelHubVisualState::Hovered:
                return 1.0f;
            }
            return 1.0f;
        };

    auto build_hub_paths =
        [&](bool is_left,
            core::SelectionWheelHubVisualMetrics const &metrics) -> HubPathBundle {
        HubPathBundle bundle{};
        bundle.metrics = metrics;
        bundle.outer_path = make_hub_path(is_left, metrics.radius_px);
        bundle.glyph_center_x = is_left ? cx - (metrics.radius_px + half_gap) / 2.f
                                        : cx + (metrics.radius_px + half_gap) / 2.f;
        if (!bundle.outer_path || !core::kTextWheelHubDrawBorder) {
            return bundle;
        }

        float const gray_inner_radius =
            metrics.radius_px - core::kSelectionWheelRingOuterBorderWidthPx;
        float const gray_half_gap =
            half_gap + core::kSelectionWheelRingOuterBorderWidthPx;
        if (gray_inner_radius > gray_half_gap) {
            bundle.gray_inner_path =
                make_hub_path(is_left, gray_inner_radius, gray_half_gap);
        }

        float const black_inner_radius =
            gray_inner_radius - core::kSelectionWheelRingInnerBorderWidthPx;
        float const black_half_gap =
            gray_half_gap + core::kSelectionWheelRingInnerBorderWidthPx;
        if (black_inner_radius > black_half_gap) {
            bundle.black_inner_path =
                make_hub_path(is_left, black_inner_radius, black_half_gap);
        }

        return bundle;
    };

    // Draw the hue-spectrum gradient rectangle glyph centered in the given hub half.
    auto draw_hue_glyph = [&](HubPathBundle const &bundle) {
        if (!res.text_wheel_hue_brush) {
            return;
        }
        float const glyph_w = core::kTextWheelHubGlyphRectWidthPx;
        float const glyph_h = core::kTextWheelHubGlyphRectHeightPx;
        D2D1_RECT_F const glyph_rect =
            D2D1::RectF(bundle.glyph_center_x - glyph_w / 2.f, cy - glyph_h / 2.f,
                        bundle.glyph_center_x + glyph_w / 2.f, cy + glyph_h / 2.f);
        res.text_wheel_hue_brush->SetStartPoint(D2D1::Point2F(glyph_rect.left, cy));
        res.text_wheel_hue_brush->SetEndPoint(D2D1::Point2F(glyph_rect.right, cy));
        rt->FillRectangle(glyph_rect, res.text_wheel_hue_brush.Get());
        if (bundle.metrics.state == core::SelectionWheelHubVisualState::Normal) {
            res.solid_brush->SetColor(
                With_alpha(hub_fill_color(bundle.metrics.state),
                           kSelectionWheelHubContentDimBlendAlpha));
            rt->FillRectangle(glyph_rect, res.solid_brush.Get());
        }
        res.solid_brush->SetColor(
            hub_content_color(bundle.metrics.state, D2D1::ColorF(0.f, 0.f, 0.f)));
        rt->DrawRectangle(glyph_rect, res.solid_brush.Get(), 1.f);
    };

    auto draw_opacity_swipe_glyph = [&](HubPathBundle const &bundle) {
        float const glyph_w = core::kTextWheelHubGlyphRectWidthPx;
        float const glyph_h = core::kTextWheelHubGlyphRectHeightPx;
        D2D1_RECT_F const glyph_rect =
            D2D1::RectF(bundle.glyph_center_x - glyph_w / 2.f, cy - glyph_h / 2.f,
                        bundle.glyph_center_x + glyph_w / 2.f, cy + glyph_h / 2.f);
        float const swipe_opacity = hub_opacity_swipe_opacity(bundle.metrics.state);
        constexpr float band_position_divisor =
            static_cast<float>(kSelectionWheelHubOpacitySwipeBandCount - 1u);
        float const band_width =
            (glyph_rect.right - glyph_rect.left) /
            static_cast<float>(kSelectionWheelHubOpacitySwipeBandCount);
        for (size_t i = 0; i < kSelectionWheelHubOpacitySwipeBandCount; ++i) {
            float const t = static_cast<float>(i) / band_position_divisor;
            float const alpha =
                (kSelectionWheelHubOpacitySwipeLeadingAlpha * (1.0f - t) +
                 kSelectionWheelHubOpacitySwipeTrailingAlpha * t) *
                swipe_opacity;
            float const band_left =
                glyph_rect.left + band_width * static_cast<float>(i);
            float const band_right = band_left + band_width + 1.0f;
            res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, alpha));
            rt->FillRectangle(
                D2D1::RectF(band_left, glyph_rect.top, band_right, glyph_rect.bottom),
                res.solid_brush.Get());
        }

        res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, swipe_opacity));
        rt->DrawRectangle(glyph_rect, res.solid_brush.Get(), 1.0f);
    };

    auto draw_hub_half = [&](HubPathBundle const &bundle) {
        if (!bundle.outer_path) {
            return;
        }

        ID2D1Geometry *fill_geometry =
            bundle.black_inner_path
                ? static_cast<ID2D1Geometry *>(bundle.black_inner_path.Get())
            : bundle.gray_inner_path
                ? static_cast<ID2D1Geometry *>(bundle.gray_inner_path.Get())
                : static_cast<ID2D1Geometry *>(bundle.outer_path.Get());

        res.solid_brush->SetColor(hub_fill_color(bundle.metrics.state));
        rt->FillGeometry(fill_geometry, res.solid_brush.Get());

        if (!core::kTextWheelHubDrawBorder) {
            return;
        }
        if (bundle.gray_inner_path) {
            Microsoft::WRL::ComPtr<ID2D1GeometryGroup> gray_band = build_segment_band(
                bundle.outer_path.Get(), bundle.gray_inner_path.Get());
            if (gray_band) {
                res.solid_brush->SetColor(
                    Colorref_to_d2d(core::kSelectionWheelRingOuterBorderColor, 1.0f));
                rt->FillGeometry(gray_band.Get(), res.solid_brush.Get());
            }
        }
        if (bundle.gray_inner_path && bundle.black_inner_path) {
            Microsoft::WRL::ComPtr<ID2D1GeometryGroup> black_band = build_segment_band(
                bundle.gray_inner_path.Get(), bundle.black_inner_path.Get());
            if (black_band) {
                res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f));
                rt->FillGeometry(black_band.Get(), res.solid_brush.Get());
            }
        }
    };

    // --- Text style hub ---
    if (has_hub) {
        bool const left_active =
            (input.text_wheel_active_mode == core::TextWheelMode::Color);
        bool const left_hovered =
            input.text_wheel_hovered_hub ==
            std::optional<core::TextWheelHubSide>{core::TextWheelHubSide::Color};
        bool const right_hovered =
            input.text_wheel_hovered_hub ==
            std::optional<core::TextWheelHubSide>{core::TextWheelHubSide::Font};
        HubPathBundle const left_bundle = build_hub_paths(
            true,
            core::Get_selection_wheel_hub_visual_metrics(left_active, left_hovered));
        HubPathBundle const right_bundle = build_hub_paths(
            false,
            core::Get_selection_wheel_hub_visual_metrics(!left_active, right_hovered));

        draw_hub_half(left_bundle);
        draw_hub_half(right_bundle);

        // Left glyph: hue gradient rectangle.
        if (left_bundle.outer_path) {
            draw_hue_glyph(left_bundle);
        }

        // Right glyph: "A" label in current font.
        if (right_bundle.outer_path && res.dwrite_factory &&
            !input.text_wheel_hub_font_family.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt = Create_text_format(
                res.dwrite_factory.Get(), input.text_wheel_hub_font_family,
                kSelectionWheelFontPreviewPointSize);
            if (fmt) {
                constexpr std::wstring_view label = L"A";
                float text_w = 0.f;
                float text_h = 0.f;
                if (Measure_text(res, fmt.Get(), label, text_w, text_h)) {
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout =
                        Create_text_layout(
                            res.dwrite_factory.Get(), fmt.Get(), label,
                            text_w + kSelectionWheelFontPreviewLayoutPaddingPx,
                            text_h + kSelectionWheelFontPreviewLayoutPaddingPx);
                    if (layout) {
                        res.solid_brush->SetColor(hub_content_color(
                            right_bundle.metrics.state, D2D1::ColorF(0.f, 0.f, 0.f)));
                        rt->DrawTextLayout(
                            D2D1::Point2F(right_bundle.glyph_center_x - text_w / 2.f,
                                          cy - text_h / 2.f),
                            layout.Get(), res.solid_brush.Get());
                    }
                }
            }
        }
    }

    // --- Highlighter opacity hub ---
    if (has_highlighter_hub) {
        bool const left_active =
            (input.highlighter_wheel_active_mode == core::HighlighterWheelMode::Color);
        bool const left_hovered = input.highlighter_wheel_hovered_hub ==
                                  std::optional<core::HighlighterWheelHubSide>{
                                      core::HighlighterWheelHubSide::Color};
        bool const right_hovered = input.highlighter_wheel_hovered_hub ==
                                   std::optional<core::HighlighterWheelHubSide>{
                                       core::HighlighterWheelHubSide::Opacity};
        HubPathBundle const left_bundle = build_hub_paths(
            true,
            core::Get_selection_wheel_hub_visual_metrics(left_active, left_hovered));
        HubPathBundle const right_bundle = build_hub_paths(
            false,
            core::Get_selection_wheel_hub_visual_metrics(!left_active, right_hovered));

        draw_hub_half(left_bundle);
        draw_hub_half(right_bundle);

        // Left glyph: hue gradient rectangle.
        if (left_bundle.outer_path) {
            draw_hue_glyph(left_bundle);
        }

        // Right glyph: generated rectangular opacity strip in black alpha bands.
        if (right_bundle.outer_path) {
            draw_opacity_swipe_glyph(right_bundle);
        }
    }
}

// ---------------------------------------------------------------------------
// Draft stroke bitmap
// ---------------------------------------------------------------------------

void Clear_bitmap_target(ID2D1BitmapRenderTarget *render_target) {
    if (render_target == nullptr) {
        return;
    }

    render_target->BeginDraw();
    render_target->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
    (void)render_target->EndDraw();
}

void Reset_draft_stroke_metadata(D2DOverlayResources &res) {
    res.draft_stroke_point_count = 0;
    res.draft_stroke_last_point = {};
    res.draft_stroke_body_raw_point_count = 0;
    res.draft_stroke_body_point_count = 0;
    res.draft_stroke_stable_tail_start_index = 0;
    res.draft_stroke_style.reset();
    res.draft_stroke_tip_shape = core::FreehandTipShape::Round;
    res.draft_stroke_smoothing_mode = core::FreehandSmoothingMode::Off;
    res.draft_stroke_bitmap_uses_cached_body = false;
}

void Clear_draft_stroke_surface(D2DOverlayResources &res) {
    if (!res.draft_stroke_bitmap && !res.draft_stroke_body_bitmap &&
        res.draft_stroke_point_count == 0) {
        return;
    }

    Reset_draft_stroke_metadata(res);
    res.draft_stroke_bitmap.Reset();
    res.draft_stroke_body_bitmap.Reset();
    Clear_bitmap_target(res.draft_stroke_rt.Get());
    Clear_bitmap_target(res.draft_stroke_body_rt.Get());
}

void Draw_preview_segments(ID2D1RenderTarget *render_target, D2DOverlayResources &res,
                           std::span<const core::PointPx> points,
                           core::StrokeStyle style, core::FreehandTipShape tip_shape) {
    if (points.empty()) {
        return;
    }
    Draw_freehand_points(render_target, res, points, style, tip_shape);
}

[[nodiscard]] core::StrokeStyle Preview_draw_style(core::StrokeStyle style,
                                                   core::FreehandTipShape tip_shape) {
    if (tip_shape == core::FreehandTipShape::Square) {
        style.opacity_percent = core::StrokeStyle::kMaxOpacityPercent;
    }
    return style;
}

[[nodiscard]] bool Can_reuse_cached_square_commit(
    D2DOverlayResources const &res, core::FreehandStrokeAnnotation const &stroke,
    size_t annotation_index, size_t annotation_count) noexcept {
    return annotation_index + 1 == annotation_count &&
           res.draft_stroke_point_count != 0 &&
           res.draft_stroke_style == std::optional<core::StrokeStyle>(stroke.style) &&
           res.draft_stroke_tip_shape == core::FreehandTipShape::Square &&
           res.draft_stroke_smoothing_mode != core::FreehandSmoothingMode::Off &&
           res.draft_stroke_body_bitmap != nullptr &&
           res.draft_stroke_body_point_count != 0 &&
           !stroke.points.empty() &&
           res.draft_stroke_last_point == stroke.points.back() &&
           res.draft_stroke_body_point_count <= stroke.points.size();
}

void Set_committed_square_mask_input(ID2D1Effect *multiply_effect,
                                     D2DOverlayResources &res,
                                     ID2D1Bitmap *tail_bitmap,
                                     bool use_cached_body) noexcept {
    if (multiply_effect == nullptr) {
        return;
    }
    if (use_cached_body && res.draft_stroke_body_bitmap &&
        res.draft_stroke_composite_effect && tail_bitmap != nullptr) {
        res.draft_stroke_composite_effect->SetInput(0,
                                                    res.draft_stroke_body_bitmap.Get());
        res.draft_stroke_composite_effect->SetInput(1, tail_bitmap);
        multiply_effect->SetInputEffect(1, res.draft_stroke_composite_effect.Get());
        return;
    }
    if (use_cached_body && res.draft_stroke_body_bitmap) {
        multiply_effect->SetInput(1, res.draft_stroke_body_bitmap.Get());
        return;
    }
    multiply_effect->SetInput(1, tail_bitmap);
}

[[nodiscard]] bool Has_matching_cached_draft_input(
    D2DOverlayResources const &res, std::span<const core::PointPx> points,
    std::optional<core::StrokeStyle> const &style, core::FreehandTipShape tip_shape,
    core::FreehandSmoothingMode smoothing_mode) noexcept {
    return res.draft_stroke_bitmap && res.draft_stroke_point_count == points.size() &&
           !points.empty() && res.draft_stroke_last_point == points.back() &&
           res.draft_stroke_style == style && res.draft_stroke_tip_shape == tip_shape &&
           res.draft_stroke_smoothing_mode == smoothing_mode;
}

void Store_draft_stroke_metadata(D2DOverlayResources &res,
                                 std::span<const core::PointPx> points,
                                 core::StrokeStyle style,
                                 core::FreehandTipShape tip_shape,
                                 core::FreehandSmoothingMode smoothing_mode,
                                 bool uses_cached_body = false) {
    res.draft_stroke_point_count = points.size();
    res.draft_stroke_last_point = points.back();
    res.draft_stroke_style = style;
    res.draft_stroke_tip_shape = tip_shape;
    res.draft_stroke_smoothing_mode = smoothing_mode;
    res.draft_stroke_bitmap_uses_cached_body = uses_cached_body;
}

[[nodiscard]] bool
Can_reuse_draft_stroke_body(D2DOverlayResources const &res, core::StrokeStyle style,
                            core::FreehandTipShape tip_shape,
                            core::FreehandSmoothingMode smoothing_mode,
                            core::FreehandPreviewPlan const &plan) noexcept {
    return res.draft_stroke_body_bitmap && plan.stable_raw_point_count != 0 &&
           res.draft_stroke_stable_tail_start_index == plan.tail_start_index &&
           res.draft_stroke_style == std::optional<core::StrokeStyle>(style) &&
           res.draft_stroke_tip_shape == tip_shape &&
           res.draft_stroke_smoothing_mode == smoothing_mode;
}

[[nodiscard]] bool Rebuild_draft_stroke_body_bitmap(
    D2DOverlayResources &res, std::span<const core::PointPx> stable_points,
    core::StrokeStyle style, core::FreehandTipShape tip_shape,
    core::FreehandSmoothingMode smoothing_mode, size_t raw_point_count,
    size_t tail_start_index) {
    if (!res.draft_stroke_body_rt) {
        return false;
    }

    res.draft_stroke_body_rt->BeginDraw();
    res.draft_stroke_body_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
    Draw_preview_segments(res.draft_stroke_body_rt.Get(), res, stable_points,
                          Preview_draw_style(style, tip_shape), tip_shape);
    HRESULT const hr = res.draft_stroke_body_rt->EndDraw();
    if (FAILED(hr)) {
        res.draft_stroke_body_bitmap.Reset();
        res.draft_stroke_body_raw_point_count = 0;
        res.draft_stroke_body_point_count = 0;
        res.draft_stroke_stable_tail_start_index = 0;
        return false;
    }

    (void)res.draft_stroke_body_rt->GetBitmap(
        res.draft_stroke_body_bitmap.ReleaseAndGetAddressOf());
    res.draft_stroke_body_raw_point_count = raw_point_count;
    res.draft_stroke_body_point_count = stable_points.size();
    res.draft_stroke_stable_tail_start_index = tail_start_index;
    res.draft_stroke_style = style;
    res.draft_stroke_tip_shape = tip_shape;
    res.draft_stroke_smoothing_mode = smoothing_mode;
    return res.draft_stroke_body_bitmap != nullptr;
}

[[nodiscard]] bool Update_incremental_square_draft_stroke_body_bitmap(
    D2DOverlayResources &res, std::span<const core::PointPx> points,
    core::StrokeStyle style, core::FreehandSmoothingMode smoothing_mode,
    core::FreehandPreviewPlan const &plan) {
    bool const cache_matches =
        res.draft_stroke_body_bitmap != nullptr &&
        res.draft_stroke_style == std::optional<core::StrokeStyle>(style) &&
        res.draft_stroke_tip_shape == core::FreehandTipShape::Square &&
        res.draft_stroke_smoothing_mode == smoothing_mode &&
        res.draft_stroke_body_raw_point_count <= plan.stable_raw_point_count;
    if (!cache_matches) {
        std::vector<core::PointPx> const smoothed_points = core::Smooth_freehand_points(
            points.first(plan.stable_raw_point_count), smoothing_mode, style.width_px);
        return Rebuild_draft_stroke_body_bitmap(
            res, smoothed_points, style, core::FreehandTipShape::Square, smoothing_mode,
            plan.stable_raw_point_count, plan.tail_start_index);
    }

    if (res.draft_stroke_body_raw_point_count == plan.stable_raw_point_count) {
        res.draft_stroke_body_raw_point_count = plan.stable_raw_point_count;
        res.draft_stroke_stable_tail_start_index = plan.tail_start_index;
        return true;
    }
    if (!res.draft_stroke_body_rt) {
        return false;
    }

    constexpr size_t square_draft_body_overlap_raw_point_count = 8;
    size_t const draw_start_index = res.draft_stroke_body_raw_point_count >
                                            square_draft_body_overlap_raw_point_count
                                        ? res.draft_stroke_body_raw_point_count -
                                              square_draft_body_overlap_raw_point_count
                                        : 0;
    std::vector<core::PointPx> const smoothed_points = core::Smooth_freehand_points(
        points.subspan(draw_start_index,
                       plan.stable_raw_point_count - draw_start_index),
        smoothing_mode, style.width_px);
    res.draft_stroke_body_rt->BeginDraw();
    Draw_preview_segments(res.draft_stroke_body_rt.Get(), res, smoothed_points,
                          Preview_draw_style(style, core::FreehandTipShape::Square),
                          core::FreehandTipShape::Square);
    HRESULT const hr = res.draft_stroke_body_rt->EndDraw();
    if (FAILED(hr)) {
        res.draft_stroke_body_bitmap.Reset();
        res.draft_stroke_body_raw_point_count = 0;
        res.draft_stroke_body_point_count = 0;
        res.draft_stroke_stable_tail_start_index = 0;
        return false;
    }

    (void)res.draft_stroke_body_rt->GetBitmap(
        res.draft_stroke_body_bitmap.ReleaseAndGetAddressOf());
    res.draft_stroke_body_raw_point_count = plan.stable_raw_point_count;
    res.draft_stroke_body_point_count = core::Smooth_freehand_points(
                                            points.first(plan.stable_raw_point_count),
                                            smoothing_mode, style.width_px)
                                            .size();
    res.draft_stroke_stable_tail_start_index = plan.tail_start_index;
    return res.draft_stroke_body_bitmap != nullptr;
}

[[nodiscard]] bool Rebuild_draft_stroke_bitmap_from_cached_body(
    D2DOverlayResources &res, std::span<const core::PointPx> points,
    core::StrokeStyle style, core::FreehandTipShape tip_shape,
    core::FreehandSmoothingMode smoothing_mode, core::FreehandPreviewPlan const &plan) {
    if (!res.draft_stroke_rt || !res.draft_stroke_body_bitmap) {
        return false;
    }

    res.draft_stroke_rt->BeginDraw();
    res.draft_stroke_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
    res.draft_stroke_rt->DrawBitmap(res.draft_stroke_body_bitmap.Get());
    Draw_preview_segments(res.draft_stroke_rt.Get(), res, plan.tail_points,
                          Preview_draw_style(style, tip_shape), tip_shape);

    HRESULT const hr = res.draft_stroke_rt->EndDraw();
    if (FAILED(hr)) {
        res.draft_stroke_bitmap.Reset();
        return false;
    }

    (void)res.draft_stroke_rt->GetBitmap(
        res.draft_stroke_bitmap.ReleaseAndGetAddressOf());
    Store_draft_stroke_metadata(res, points, style, tip_shape, smoothing_mode);
    return res.draft_stroke_bitmap != nullptr;
}

[[nodiscard]] bool Rebuild_square_draft_stroke_tail_bitmap(
    D2DOverlayResources &res, std::span<const core::PointPx> points,
    core::StrokeStyle style, core::FreehandSmoothingMode smoothing_mode,
    core::FreehandPreviewPlan const &plan) {
    if (!res.draft_stroke_rt) {
        return false;
    }

    res.draft_stroke_rt->BeginDraw();
    res.draft_stroke_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
    Draw_preview_segments(res.draft_stroke_rt.Get(), res, plan.tail_points,
                          Preview_draw_style(style, core::FreehandTipShape::Square),
                          core::FreehandTipShape::Square);

    HRESULT const hr = res.draft_stroke_rt->EndDraw();
    if (FAILED(hr)) {
        res.draft_stroke_bitmap.Reset();
        return false;
    }

    (void)res.draft_stroke_rt->GetBitmap(
        res.draft_stroke_bitmap.ReleaseAndGetAddressOf());
    Store_draft_stroke_metadata(res, points, style, core::FreehandTipShape::Square,
                                smoothing_mode,
                                /*uses_cached_body=*/true);
    return res.draft_stroke_bitmap != nullptr;
}

// Rebuilds the current freehand draft bitmap from the full live stroke each frame.
// The stable body may be smoothed while the newest tail stays raw so the cursor tip
// remains visually attached during live drawing.
// Must be called BEFORE hwnd_rt->BeginDraw.
void Update_draft_stroke_bitmap(D2DOverlayResources &res,
                                std::span<const core::PointPx> points,
                                std::optional<core::StrokeStyle> const &style,
                                core::FreehandTipShape tip_shape,
                                core::FreehandSmoothingMode smoothing_mode) {
    if (points.empty() || !style.has_value()) {
        Clear_draft_stroke_surface(res);
        return;
    }
    if (Has_matching_cached_draft_input(res, points, style, tip_shape,
                                        smoothing_mode)) {
        return;
    }
    if (!res.draft_stroke_rt) {
        return;
    }
    core::StrokeStyle const preview_draw_style = Preview_draw_style(*style, tip_shape);

    bool const uses_smoothed_preview =
        smoothing_mode != core::FreehandSmoothingMode::Off && points.size() > 2;
    std::optional<core::FreehandPreviewPlan> preview_plan = std::nullopt;
    if (uses_smoothed_preview) {
        preview_plan.emplace(
            core::Build_freehand_preview_plan(points, smoothing_mode, style->width_px));
        bool drew_from_cached_body = false;
        if (preview_plan->stable_raw_point_count != 0) {
            bool body_ready = false;
            if (tip_shape == core::FreehandTipShape::Square) {
                body_ready = Update_incremental_square_draft_stroke_body_bitmap(
                    res, points, *style, smoothing_mode, *preview_plan);
            } else {
                bool const reused_body = Can_reuse_draft_stroke_body(
                    res, *style, tip_shape, smoothing_mode, *preview_plan);
                body_ready = reused_body ||
                             Rebuild_draft_stroke_body_bitmap(
                                 res,
                                 core::Smooth_freehand_points(
                                     points.first(preview_plan->stable_raw_point_count),
                                     smoothing_mode, style->width_px),
                                 *style, tip_shape, smoothing_mode,
                                 preview_plan->stable_raw_point_count,
                                 preview_plan->tail_start_index);
            }
            if (body_ready) {
                if (tip_shape == core::FreehandTipShape::Square &&
                    res.draft_stroke_composite_effect) {
                    drew_from_cached_body = Rebuild_square_draft_stroke_tail_bitmap(
                        res, points, *style, smoothing_mode, *preview_plan);
                } else {
                    drew_from_cached_body =
                        Rebuild_draft_stroke_bitmap_from_cached_body(
                            res, points, *style, tip_shape, smoothing_mode,
                            *preview_plan);
                }
            }
        }

        if (drew_from_cached_body) {
            return;
        }

        if (preview_plan->stable_raw_point_count == 0) {
            res.draft_stroke_body_bitmap.Reset();
            res.draft_stroke_body_raw_point_count = 0;
            res.draft_stroke_body_point_count = 0;
            res.draft_stroke_stable_tail_start_index = 0;
        }
    } else {
        res.draft_stroke_body_bitmap.Reset();
        res.draft_stroke_body_raw_point_count = 0;
        res.draft_stroke_body_point_count = 0;
        res.draft_stroke_stable_tail_start_index = 0;
    }

    res.draft_stroke_rt->BeginDraw();
    res.draft_stroke_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));

    if (!preview_plan.has_value() || preview_plan->stable_raw_point_count == 0) {
        Draw_preview_segments(res.draft_stroke_rt.Get(), res, points,
                              preview_draw_style, tip_shape);
    } else {
        Draw_preview_segments(res.draft_stroke_rt.Get(), res,
                              core::Smooth_freehand_points(
                                  points.first(preview_plan->stable_raw_point_count),
                                  smoothing_mode, style->width_px),
                              preview_draw_style, tip_shape);
        Draw_preview_segments(res.draft_stroke_rt.Get(), res, preview_plan->tail_points,
                              preview_draw_style, tip_shape);
    }

    HRESULT const hr = res.draft_stroke_rt->EndDraw();
    if (SUCCEEDED(hr)) {
        (void)res.draft_stroke_rt->GetBitmap(
            res.draft_stroke_bitmap.ReleaseAndGetAddressOf());
        Store_draft_stroke_metadata(res, points, *style, tip_shape, smoothing_mode);
    }
}

void Set_live_square_draft_mask_input(ID2D1Effect *multiply_effect,
                                      D2DOverlayResources &res) noexcept {
    if (multiply_effect == nullptr) {
        return;
    }
    if (res.draft_stroke_bitmap_uses_cached_body && res.draft_stroke_body_bitmap &&
        res.draft_stroke_bitmap && res.draft_stroke_composite_effect) {
        res.draft_stroke_composite_effect->SetInput(0,
                                                    res.draft_stroke_body_bitmap.Get());
        res.draft_stroke_composite_effect->SetInput(1, res.draft_stroke_bitmap.Get());
        multiply_effect->SetInputEffect(1, res.draft_stroke_composite_effect.Get());
        return;
    }
    multiply_effect->SetInput(1, res.draft_stroke_bitmap.Get());
}

// ---------------------------------------------------------------------------
// Live layer (drawn every frame)
// ---------------------------------------------------------------------------

[[nodiscard]] bool Has_live_annotation_draft(D2DPaintInput const &input) noexcept {
    return input.draft_annotation != nullptr ||
           input.draft_text_annotation != nullptr ||
           (!input.draft_freehand_points.empty() &&
            input.draft_freehand_style.has_value());
}

void Draw_live_annotation_draft(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                                D2DPaintInput const &input) {
    if (input.draft_text_annotation != nullptr) {
        Draw_draft_text(rt, res, input);
    } else if (input.draft_annotation != nullptr) {
        Draw_annotation(rt, res, *input.draft_annotation);
    } else if (!input.draft_freehand_points.empty() &&
               input.draft_freehand_style.has_value()) {
        if (res.draft_stroke_bitmap) {
            if (input.draft_freehand_tip_shape == core::FreehandTipShape::Square &&
                res.screenshot && res.multiply_effect) {
                // Multiply blend: the preview stroke bitmap is an opaque coverage mask
                // tinted to the selected highlighter color. k1 applies the user
                // opacity once at compose time. Non-stroke pixels stay transparent and
                // leave the destination unchanged.
                Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc;
                if (SUCCEEDED(rt->QueryInterface(IID_PPV_ARGS(&dc)))) {
                    if (res.draft_stroke_bitmap == nullptr) {
                        return;
                    }
                    D2D1_VECTOR_4F const coeffs = {input.draft_freehand_blit_opacity,
                                                   0.0f, 0.0f, 0.0f};
                    (void)res.multiply_effect->SetValue(
                        D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, coeffs);
                    res.multiply_effect->SetInput(0, res.screenshot.Get());
                    Set_live_square_draft_mask_input(res.multiply_effect.Get(), res);
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
}

void Draw_live_layer(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                     D2DPaintInput const &input, int vd_width, int vd_height,
                     bool draw_annotation_draft = true) {
    // Draft annotation (while gesture is active).
    if (draw_annotation_draft) {
        Draw_live_annotation_draft(rt, res, input);
    }

    // Selection border: live_rect while dragging; final_selection otherwise.
    // Dashed during drag (rubber-band), solid once the selection is committed.
    bool const interacting = input.dragging || input.handle_dragging ||
                             input.move_dragging || input.modifier_preview;
    core::RectPx const disp_sel = interacting ? input.live_rect : input.final_selection;
    Draw_selection_border(rt, res, disp_sel, /*dashed=*/input.dragging);
    if (input.annotation_selection_dragging &&
        !input.annotation_selection_live_rect.Is_empty()) {
        Draw_selected_annotation_marquee(rt, res, input.annotation_selection_live_rect,
                                         input.selected_annotation_marquee_phase_px);
    }

    // Dimension labels (only while interacting).
    if (interacting && !disp_sel.Is_empty()) {
        Draw_dimension_labels(
            rt, res, disp_sel, input.selection_size_override, vd_width, vd_height,
            input.show_selection_size_side_labels,
            input.show_selection_size_center_label && !input.move_dragging,
            input.show_offscreen_capture_note);
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

    // Selected annotation chrome.
    if (input.selected_annotation_bounds.has_value()) {
        Draw_selected_annotation_marquee(rt, res, input.selected_annotation_bounds,
                                         input.selected_annotation_marquee_phase_px);
        if (input.selected_annotation != nullptr) {
            Draw_annotation_handles(rt, res, input.selected_annotation);
        }
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
                                     input.selection_wheel_font_families);
        }

        if (has_clip) {
            rt->PopLayer();
        }
    }

    // Toolbar buttons + tooltip.
    Draw_toolbar(rt, res, input);

    // Color wheel.
    Draw_selection_wheel(rt, res, input);
}

// Draws all annotations to rt using multiply blend for square-tip freehand.
// rt must be in BeginDraw state on entry and will remain so on return.
// Temporarily EndDraw/BeginDraw's rt around each highlighter so draft_stroke_rt
// can be used as a scratch surface.
void Draw_annotations_to_rt(ID2D1RenderTarget *rt, D2DOverlayResources &res,
                            std::span<const core::Annotation> annotations,
                            std::span<const AnnotationPreviewPatch> patches,
                            std::optional<uint64_t> skip_id) {

    auto const is_highlighter = [](core::Annotation const &ann) -> bool {
        auto const *fh = std::get_if<core::FreehandStrokeAnnotation>(&ann.data);
        return fh && fh->freehand_tip_shape == core::FreehandTipShape::Square;
    };

    // Invariant: patches never replace a non-highlighter with a highlighter, so the
    // can_multiply check over the base span is correct for the obfuscate preview
    // scenario.
    bool const can_multiply =
        res.screenshot && res.multiply_effect && res.draft_stroke_rt &&
        std::any_of(annotations.begin(), annotations.end(), is_highlighter);

    auto const find_patch = [&](size_t i) -> core::Annotation const * {
        for (auto const &p : patches) {
            if (p.index == i) return &p.annotation;
        }
        return nullptr;
    };

    for (size_t i = 0; i < annotations.size(); ++i) {
        if (skip_id.has_value() && annotations[i].id == *skip_id) {
            continue;
        }
        auto const *const patch = find_patch(i);
        core::Annotation const &ann = patch != nullptr ? *patch : annotations[i];
        if (can_multiply && is_highlighter(ann)) {
            auto const &fh = *std::get_if<core::FreehandStrokeAnnotation>(&ann.data);
            bool const use_cached_body =
                Can_reuse_cached_square_commit(res, fh, i, annotations.size());
            bool const cached_body_covers_annotation =
                use_cached_body &&
                res.draft_stroke_body_point_count >= fh.points.size();
            size_t const tail_start_index =
                use_cached_body && res.draft_stroke_body_point_count > 1
                    ? res.draft_stroke_body_point_count - 1
                    : 0;
            std::span<const core::PointPx> const annotation_points = fh.points;
            std::span<const core::PointPx> scratch_points =
                cached_body_covers_annotation
                    ? std::span<const core::PointPx>{}
                    : (use_cached_body ? annotation_points.subspan(tail_start_index)
                                       : annotation_points);
            core::StrokeStyle const scratch_style =
                use_cached_body ? Preview_draw_style(fh.style, fh.freehand_tip_shape)
                                : fh.style;
            float const multiply_alpha =
                use_cached_body ? Alpha_from_opacity_percent(fh.style.opacity_percent)
                                : 1.0f;
            core::RectPx const highlighter_bounds =
                core::Annotation_visual_bounds(ann).Normalized();
            if (highlighter_bounds.Is_empty()) {
                continue;
            }
            D2D1_RECT_F const highlighter_rect = Rect(highlighter_bounds);
            D2D1_POINT_2F const highlighter_target_offset =
                D2D1::Point2F(highlighter_rect.left, highlighter_rect.top);

            bool drew_multiply = false;
            bool rt_needs_begin = false;
            Microsoft::WRL::ComPtr<ID2D1Bitmap> stroke_bmp;
            bool const needs_scratch = !cached_body_covers_annotation;
            if (needs_scratch) {
                // Suspend rt so draft_stroke_rt can be used as scratch.
                if (FAILED(rt->EndDraw())) {
                    rt->BeginDraw();
                    continue;
                }
                rt_needs_begin = true;

                {
                    res.draft_stroke_rt->BeginDraw();
                    res.draft_stroke_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
                    Draw_freehand_points(res.draft_stroke_rt.Get(), res, scratch_points,
                                         scratch_style, fh.freehand_tip_shape);
                }
                if (FAILED(res.draft_stroke_rt->EndDraw()) ||
                    FAILED(res.draft_stroke_rt->GetBitmap(
                        stroke_bmp.ReleaseAndGetAddressOf()))) {
                    rt->BeginDraw();
                    rt_needs_begin = false;
                }
            }

            if (!rt_needs_begin || stroke_bmp != nullptr || cached_body_covers_annotation) {
                if (rt_needs_begin) {
                    rt->BeginDraw();
                    rt_needs_begin = false;
                }

                // k1 carries user opacity when reusing the preview's opaque mask cache.
                D2D1_VECTOR_4F const coeffs = {multiply_alpha, 0.0f, 0.0f, 0.0f};
                (void)res.multiply_effect->SetValue(
                    D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, coeffs);
                res.multiply_effect->SetInput(0, res.screenshot.Get());
                Set_committed_square_mask_input(res.multiply_effect.Get(), res,
                                                stroke_bmp.Get(),
                                                use_cached_body);
                Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc;
                if (SUCCEEDED(rt->QueryInterface(IID_PPV_ARGS(&dc)))) {
                    dc->DrawImage(res.multiply_effect.Get(), highlighter_target_offset,
                                  highlighter_rect,
                                  D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                                  D2D1_COMPOSITE_MODE_SOURCE_OVER);
                    drew_multiply = true;
                }
            }
            if (!drew_multiply) {
                // Fallback: re-enter and draw with normal SOURCE_OVER.
                if (rt_needs_begin) {
                    rt->BeginDraw();
                }
                Draw_freehand_points(rt, res, fh.points, fh.style,
                                     fh.freehand_tip_shape);
            }
        } else {
            Draw_annotation(rt, res, ann);
        }
    }

    // If draft_stroke_rt was used as scratch, clear only the composed draft bitmap so
    // the next live frame can recombine it from any still-valid cached body/tail input.
    if (can_multiply) {
        res.draft_stroke_point_count = 0;
        res.draft_stroke_bitmap.Reset();
        Clear_bitmap_target(res.draft_stroke_rt.Get());
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Rebuild_annotations_bitmap(D2DOverlayResources &res,
                                std::span<const core::Annotation> annotations,
                                std::span<const AnnotationPreviewPatch> patches,
                                std::optional<uint64_t> skip_id) {

    if (!res.annotations_rt) {
        return;
    }

    res.annotations_rt->BeginDraw();
    res.annotations_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
    Draw_annotations_to_rt(res.annotations_rt.Get(), res, annotations, patches,
                           skip_id);

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

    // Composite committed annotations before dimming so the dim sits on top of them.
    res.frozen_rt->DrawBitmap(res.annotations_bitmap.Get());

    // Dim the entire capture (on top of screenshot and all annotations).
    res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, kOverlayDimAlpha));
    res.frozen_rt->FillRectangle(full, res.solid_brush.Get());

    // Restore the selection area undimmed: screenshot then annotations, both
    // clipped so nothing outside the selection punches through the dim.
    if (!selection.Is_empty()) {
        Draw_clipped_screenshot_rect(res.frozen_rt.Get(), res.screenshot.Get(),
                                     selection, vd_width, vd_height);
        res.frozen_rt->PushAxisAlignedClip(Rect(selection),
                                           D2D1_ANTIALIAS_MODE_ALIASED);
        res.frozen_rt->DrawBitmap(res.annotations_bitmap.Get());
        res.frozen_rt->PopAxisAlignedClip();
    }

    HRESULT const hr = res.frozen_rt->EndDraw();
    if (SUCCEEDED(hr)) {
        (void)res.frozen_rt->GetBitmap(res.frozen_bitmap.ReleaseAndGetAddressOf());
        res.frozen_valid = true;
    }
}

bool Paint_d2d_frame(D2DOverlayResources &res, D2DPaintInput const &input, int vd_width,
                     int vd_height, IOverlayTopLayer *top_layer) {
    if (!res.hwnd_rt) {
        return true;
    }

    Update_draft_stroke_bitmap(
        res, input.draft_freehand_points, input.draft_freehand_style,
        input.draft_freehand_tip_shape, input.draft_freehand_smoothing_mode);

    // When an annotation edit interaction is active, the annotation under the cursor
    // changes every frame. Rebuild annotations_bitmap with the live state now, before
    // hwnd_rt->BeginDraw(), so the normal blit path gets the correct multiply-blend
    // rendering for highlighters without any mid-frame EndDraw on hwnd_rt.
    if (input.annotation_editing) {
        Rebuild_annotations_bitmap(res, input.annotations, input.annotation_patches);
    }

    // Any live annotation draft must be drawn before the dim in the dynamic path so
    // the dim sits on top outside the selection. Force the dynamic path whenever one
    // is present.
    bool const has_live_annotation_draft = Has_live_annotation_draft(input);
    bool const is_steady_state =
        res.frozen_valid && !input.dragging && !input.handle_dragging &&
        !input.move_dragging && !input.annotation_editing && !input.modifier_preview &&
        !has_live_annotation_draft && input.lifted_window_bitmap == nullptr;

    res.hwnd_rt->BeginDraw();

    if (is_steady_state) {
        // Fastest path: one GPU blit of the frozen composite.
        if (res.frozen_bitmap) {
            res.hwnd_rt->DrawBitmap(res.frozen_bitmap.Get());
        }
    } else {
        // Dynamic path: rebuild per frame from screenshot + annotations + dim +
        // selection restore.
        D2D1_RECT_F const full = D2D1::RectF(0.f, 0.f, static_cast<float>(vd_width),
                                             static_cast<float>(vd_height));
        if (res.screenshot) {
            res.hwnd_rt->DrawBitmap(res.screenshot.Get());
        }

        // Composite annotations before dimming so the dim sits on top of them.
        if (res.annotations_bitmap) {
            res.hwnd_rt->DrawBitmap(res.annotations_bitmap.Get());
        }

        // Draft annotations also draw before the dim for the same reason.
        if (has_live_annotation_draft) {
            Draw_live_annotation_draft(res.hwnd_rt.Get(), res, input);
        }

        // Dim the entire canvas (on top of screenshot and all annotations).
        res.solid_brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, kOverlayDimAlpha));
        res.hwnd_rt->FillRectangle(full, res.solid_brush.Get());

        // Restore selection area undimmed: screenshot, annotations, and live draft
        // visuals — all clipped to the selection rect.
        // Use live_rect while dragging the selection; fall back to final_selection
        // when an annotation tool gesture is active (live_rect is empty then).
        core::RectPx const restore_rect =
            !input.live_rect.Is_empty() ? input.live_rect : input.final_selection;
        if (!restore_rect.Is_empty()) {
            if (input.lifted_window_bitmap != nullptr &&
                !input.lifted_window_dest_rect.Is_empty() &&
                !input.lifted_window_source_rect.Is_empty()) {
                Draw_bitmap_rect(res.hwnd_rt.Get(), input.lifted_window_bitmap,
                                 input.lifted_window_dest_rect,
                                 input.lifted_window_source_rect);
            } else if (res.screenshot) {
                Draw_clipped_screenshot_rect(res.hwnd_rt.Get(), res.screenshot.Get(),
                                             restore_rect, vd_width, vd_height);
            }
        }
        if (!restore_rect.Is_empty()) {
            res.hwnd_rt->PushAxisAlignedClip(Rect(restore_rect),
                                             D2D1_ANTIALIAS_MODE_ALIASED);
            if (res.annotations_bitmap) {
                res.hwnd_rt->DrawBitmap(res.annotations_bitmap.Get());
            }
            if (has_live_annotation_draft) {
                Draw_live_annotation_draft(res.hwnd_rt.Get(), res, input);
            }
            res.hwnd_rt->PopAxisAlignedClip();
        }
    }

    // Draft visuals were already composited in the dynamic path above; skip them here
    // so Draw_live_layer does not draw them again on top of the dim.
    Draw_live_layer(res.hwnd_rt.Get(), res, input, vd_width, vd_height,
                    /*draw_annotation_draft=*/!has_live_annotation_draft);

    if (top_layer != nullptr && top_layer->Is_visible()) {
        (void)top_layer->Paint_d2d(res.hwnd_rt.Get(), res.dwrite_factory.Get(),
                                   res.solid_brush.Get());
    }

    HRESULT const hr = res.hwnd_rt->EndDraw();
    return hr != D2DERR_RECREATE_TARGET;
}

} // namespace greenflame
