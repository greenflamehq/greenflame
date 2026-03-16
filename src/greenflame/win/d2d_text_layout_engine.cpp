#include "greenflame/win/d2d_text_layout_engine.h"

namespace greenflame {

namespace {

constexpr float kDefaultDpi = 96.0f;
constexpr float kLayoutMaxExtentPx = 32768.0f;
constexpr float kRoundToNearestOffsetPx = 0.5f;
constexpr float kColorChannelMaxF = 255.0f;
constexpr wchar_t kEmptyLayoutPlaceholder[] = L"M";
struct ScopedCoInit final {
    ScopedCoInit() = default;
    ScopedCoInit(ScopedCoInit const &) = delete;
    ScopedCoInit &operator=(ScopedCoInit const &) = delete;
    ScopedCoInit(ScopedCoInit &&other) noexcept : owned(other.owned) {
        other.owned = false;
    }

    bool owned = false;

    ~ScopedCoInit() {
        if (owned) {
            CoUninitialize();
        }
    }
};

[[nodiscard]] int32_t Floor_to_int(float value) noexcept {
    int32_t const truncated = static_cast<int32_t>(value);
    return static_cast<float>(truncated) > value ? truncated - 1 : truncated;
}

[[nodiscard]] int32_t Ceil_to_int(float value) noexcept {
    int32_t const truncated = static_cast<int32_t>(value);
    return static_cast<float>(truncated) < value ? truncated + 1 : truncated;
}

[[nodiscard]] int32_t Round_to_int(float value) noexcept {
    return value >= 0.0f ? static_cast<int32_t>(value + kRoundToNearestOffsetPx)
                         : static_cast<int32_t>(value - kRoundToNearestOffsetPx);
}

[[nodiscard]] bool Text_runs_have_text(std::span<const core::TextRun> runs) noexcept {
    for (core::TextRun const &run : runs) {
        if (!run.text.empty()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int32_t
Flattened_text_length(std::span<const core::TextRun> runs) noexcept {
    int32_t length = 0;
    for (core::TextRun const &run : runs) {
        length += static_cast<int32_t>(run.text.size());
    }
    return length;
}

[[nodiscard]] std::wstring_view
Default_font_family(core::TextFontChoice choice) noexcept {
    switch (choice) {
    case core::TextFontChoice::Sans:
        return L"Arial";
    case core::TextFontChoice::Serif:
        return L"Times New Roman";
    case core::TextFontChoice::Mono:
        return L"Courier New";
    case core::TextFontChoice::Art:
        return L"Comic Sans MS";
    }
    return L"Arial";
}

[[nodiscard]] constexpr size_t
Text_font_choice_index(core::TextFontChoice choice) noexcept {
    switch (choice) {
    case core::TextFontChoice::Sans:
        return 0;
    case core::TextFontChoice::Serif:
        return 1;
    case core::TextFontChoice::Mono:
        return 2;
    case core::TextFontChoice::Art:
        return 3;
    }
    return 0;
}

struct StyledRange final {
    DWRITE_TEXT_RANGE range = {};
    core::TextStyleFlags flags = {};
};

struct LayoutBuildData final {
    std::wstring text = {};
    std::vector<StyledRange> styled_ranges = {};
};

[[nodiscard]] LayoutBuildData Build_layout_data(std::span<const core::TextRun> runs) {
    LayoutBuildData data{};
    UINT32 position = 0;
    for (core::TextRun const &run : runs) {
        data.text += run.text;
        UINT32 const length = static_cast<UINT32>(run.text.size());
        if (length > 0) {
            data.styled_ranges.push_back(
                StyledRange{DWRITE_TEXT_RANGE{position, length}, run.flags});
            position += length;
        }
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
Create_text_format(IDWriteFactory *factory,
                   core::TextAnnotationBaseStyle const &base_style,
                   std::array<std::wstring, 4> const &font_families) {
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    if (factory == nullptr) {
        return format;
    }

    size_t const family_index = Text_font_choice_index(base_style.font_choice);
    std::wstring_view const family =
        font_families[family_index].empty()
            ? Default_font_family(base_style.font_choice)
            : std::wstring_view(font_families[family_index]);
    HRESULT const hr = factory->CreateTextFormat(
        family.data(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(base_style.point_size), L"",
        format.GetAddressOf());
    if (FAILED(hr) || !format) {
        format.Reset();
        return format;
    }

    (void)format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    (void)format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    (void)format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return format;
}

[[nodiscard]] Microsoft::WRL::ComPtr<IDWriteTextLayout>
Create_text_layout(IDWriteFactory *factory, IDWriteTextFormat *format,
                   std::wstring_view text) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (factory == nullptr || format == nullptr) {
        return layout;
    }

    HRESULT const hr = factory->CreateTextLayout(
        text.data(), static_cast<UINT32>(text.size()), format, kLayoutMaxExtentPx,
        kLayoutMaxExtentPx, layout.GetAddressOf());
    if (FAILED(hr) || !layout) {
        layout.Reset();
    }
    return layout;
}

void Apply_styled_ranges(IDWriteTextLayout *layout,
                         std::span<const StyledRange> styled_ranges) {
    if (layout == nullptr) {
        return;
    }

    for (StyledRange const &styled_range : styled_ranges) {
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

[[nodiscard]] core::RectPx Rect_from_left_top_width_height(float left, float top,
                                                           float width,
                                                           float height) noexcept {
    return core::RectPx::From_ltrb(Floor_to_int(left), Floor_to_int(top),
                                   Ceil_to_int(left + width),
                                   Ceil_to_int(top + height));
}

[[nodiscard]] core::RectPx Caret_rect_from_hit_metrics(core::PointPx origin, float x,
                                                       float y,
                                                       DWRITE_HIT_TEST_METRICS const &m,
                                                       int32_t width_px) noexcept {
    int32_t const left = origin.x + Floor_to_int(x);
    int32_t const top = origin.y + Floor_to_int(y);
    int32_t const height_px = std::max(1, Ceil_to_int(m.height));
    return core::RectPx::From_ltrb(left, top, left + std::max(1, width_px),
                                   top + height_px);
}

[[nodiscard]] ScopedCoInit Ensure_com_initialized() noexcept {
    ScopedCoInit guard{};
    HRESULT const hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        guard.owned = true;
    }
    return guard;
}

[[nodiscard]] D2D1_COLOR_F Colorref_to_d2d(COLORREF color) noexcept {
    return D2D1::ColorF(static_cast<float>(GetRValue(color)) / kColorChannelMaxF,
                        static_cast<float>(GetGValue(color)) / kColorChannelMaxF,
                        static_cast<float>(GetBValue(color)) / kColorChannelMaxF, 1.0f);
}

[[nodiscard]] bool Build_text_layout(IDWriteFactory *factory,
                                     core::TextAnnotationBaseStyle const &base_style,
                                     std::span<const core::TextRun> runs,
                                     std::array<std::wstring, 4> const &font_families,
                                     Microsoft::WRL::ComPtr<IDWriteTextFormat> &format,
                                     Microsoft::WRL::ComPtr<IDWriteTextLayout> &layout,
                                     LayoutBuildData &data) {
    data = Build_layout_data(runs);
    format = Create_text_format(factory, base_style, font_families);
    if (!format) {
        return false;
    }
    layout = Create_text_layout(factory, format.Get(), data.text);
    if (!layout) {
        return false;
    }
    Apply_styled_ranges(layout.Get(), data.styled_ranges);
    return true;
}

[[nodiscard]] bool
Build_placeholder_layout(IDWriteFactory *factory,
                         core::TextAnnotationBaseStyle const &base_style,
                         std::array<std::wstring, 4> const &font_families,
                         Microsoft::WRL::ComPtr<IDWriteTextFormat> &format,
                         Microsoft::WRL::ComPtr<IDWriteTextLayout> &layout) {
    format = Create_text_format(factory, base_style, font_families);
    if (!format) {
        return false;
    }
    layout = Create_text_layout(factory, format.Get(), kEmptyLayoutPlaceholder);
    return layout != nullptr;
}

[[nodiscard]] int32_t
Insertion_offset_from_hit_test(DWRITE_HIT_TEST_METRICS const &metrics,
                               BOOL trailing_hit, int32_t text_length) noexcept {
    int32_t const trailing_delta =
        trailing_hit != FALSE ? static_cast<int32_t>(metrics.length) : 0;
    return std::clamp(static_cast<int32_t>(metrics.textPosition) + trailing_delta, 0,
                      text_length);
}

} // namespace

D2DTextLayoutEngine::D2DTextLayoutEngine(ID2D1Factory *d2d_factory,
                                         IDWriteFactory *dwrite_factory) noexcept
    : d2d_factory_(d2d_factory), dwrite_factory_(dwrite_factory) {
    Set_font_families(core::kDefaultTextFontFamilies);
}

void D2DTextLayoutEngine::Set_font_families(std::array<std::wstring_view, 4> families) {
    for (size_t index = 0; index < font_families_.size(); ++index) {
        std::wstring_view const family = families[index].empty()
                                             ? core::kDefaultTextFontFamilies[index]
                                             : families[index];
        font_families_[index].assign(family);
    }
}

core::DraftTextLayoutResult
D2DTextLayoutEngine::Build_draft_layout(core::TextDraftBuffer const &buf,
                                        core::PointPx origin) {
    core::DraftTextLayoutResult result{};
    if (dwrite_factory_ == nullptr) {
        return result;
    }

    int32_t const text_length = Flattened_text_length(buf.runs);
    if (text_length == 0) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (!Build_placeholder_layout(dwrite_factory_, buf.base_style, font_families_,
                                      format, layout)) {
            int32_t const fallback_height =
                std::max(1, (buf.base_style.point_size * 96 + 71) / 72);
            result.caret_rect = core::RectPx::From_ltrb(
                origin.x, origin.y, origin.x + 1, origin.y + fallback_height);
            result.overwrite_caret_rect =
                core::RectPx::From_ltrb(origin.x, origin.y, origin.x + fallback_height,
                                        origin.y + fallback_height);
            return result;
        }

        float caret_x = 0.0f;
        float caret_y = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (SUCCEEDED(
                layout->HitTestTextPosition(0, FALSE, &caret_x, &caret_y, &metrics))) {
            result.caret_rect =
                Caret_rect_from_hit_metrics(origin, caret_x, caret_y, metrics, 1);
            float next_x = caret_x;
            float next_y = caret_y;
            DWRITE_HIT_TEST_METRICS next_metrics{};
            int32_t overwrite_width = std::max(1, Ceil_to_int(metrics.width));
            if (SUCCEEDED(layout->HitTestTextPosition(1, FALSE, &next_x, &next_y,
                                                      &next_metrics))) {
                overwrite_width = std::max(1, Round_to_int(next_x - caret_x));
            }
            result.overwrite_caret_rect = Caret_rect_from_hit_metrics(
                origin, caret_x, caret_y, metrics, overwrite_width);
        }
        return result;
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    LayoutBuildData data{};
    if (!Build_text_layout(dwrite_factory_, buf.base_style, buf.runs, font_families_,
                           format, layout, data)) {
        return result;
    }

    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
        result.visual_bounds = Rect_from_left_top_width_height(
            static_cast<float>(origin.x) + metrics.left,
            static_cast<float>(origin.y) + metrics.top,
            metrics.widthIncludingTrailingWhitespace, metrics.height);
    }

    int32_t const selection_start =
        std::min(buf.selection.anchor_utf16, buf.selection.active_utf16);
    int32_t const selection_end =
        std::max(buf.selection.anchor_utf16, buf.selection.active_utf16);
    if (selection_start < selection_end) {
        UINT32 actual_count = 0;
        (void)layout->HitTestTextRange(
            static_cast<UINT32>(selection_start),
            static_cast<UINT32>(selection_end - selection_start),
            static_cast<float>(origin.x), static_cast<float>(origin.y), nullptr, 0,
            &actual_count);
        if (actual_count > 0) {
            std::vector<DWRITE_HIT_TEST_METRICS> hit_metrics(actual_count);
            if (SUCCEEDED(layout->HitTestTextRange(
                    static_cast<UINT32>(selection_start),
                    static_cast<UINT32>(selection_end - selection_start),
                    static_cast<float>(origin.x), static_cast<float>(origin.y),
                    hit_metrics.data(), actual_count, &actual_count))) {
                hit_metrics.resize(actual_count);
                result.selection_rects.reserve(hit_metrics.size());
                for (DWRITE_HIT_TEST_METRICS const &metric : hit_metrics) {
                    if (metric.width <= 0.0f || metric.height <= 0.0f) {
                        continue;
                    }
                    result.selection_rects.push_back(Rect_from_left_top_width_height(
                        metric.left, metric.top, metric.width, metric.height));
                }
            }
        }
    }

    float caret_x = 0.0f;
    float caret_y = 0.0f;
    DWRITE_HIT_TEST_METRICS caret_metrics{};
    if (SUCCEEDED(layout->HitTestTextPosition(
            static_cast<UINT32>(std::clamp(buf.selection.active_utf16, 0, text_length)),
            FALSE, &caret_x, &caret_y, &caret_metrics))) {
        result.caret_rect =
            Caret_rect_from_hit_metrics(origin, caret_x, caret_y, caret_metrics, 1);
        result.preferred_x_px = Round_to_int(caret_x);

        int32_t overwrite_width = 1;
        int32_t const active_offset =
            std::clamp(buf.selection.active_utf16, 0, text_length);
        if (active_offset < static_cast<int32_t>(data.text.size()) &&
            data.text[static_cast<size_t>(active_offset)] != L'\n') {
            float next_x = caret_x;
            float next_y = caret_y;
            DWRITE_HIT_TEST_METRICS next_metrics{};
            if (SUCCEEDED(layout->HitTestTextPosition(
                    static_cast<UINT32>(active_offset + 1), FALSE, &next_x, &next_y,
                    &next_metrics))) {
                overwrite_width = std::max(1, Round_to_int(next_x - caret_x));
            } else {
                overwrite_width = std::max(1, Ceil_to_int(caret_metrics.width));
            }
        }
        result.overwrite_caret_rect = Caret_rect_from_hit_metrics(
            origin, caret_x, caret_y, caret_metrics, overwrite_width);
    }

    return result;
}

int32_t D2DTextLayoutEngine::Hit_test_point(core::TextDraftBuffer const &buf,
                                            core::PointPx origin, core::PointPx point) {
    int32_t const text_length = Flattened_text_length(buf.runs);
    if (dwrite_factory_ == nullptr || text_length == 0) {
        return 0;
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    LayoutBuildData data{};
    if (!Build_text_layout(dwrite_factory_, buf.base_style, buf.runs, font_families_,
                           format, layout, data)) {
        return 0;
    }

    BOOL trailing_hit = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    HRESULT const hr = layout->HitTestPoint(static_cast<float>(point.x - origin.x),
                                            static_cast<float>(point.y - origin.y),
                                            &trailing_hit, &inside, &metrics);
    (void)inside;
    if (FAILED(hr)) {
        return 0;
    }

    return Insertion_offset_from_hit_test(metrics, trailing_hit, text_length);
}

int32_t D2DTextLayoutEngine::Move_vertical(core::TextDraftBuffer const &buf,
                                           core::PointPx origin, int32_t offset,
                                           int delta_lines, int32_t preferred_x_px) {
    (void)origin;
    int32_t const text_length = Flattened_text_length(buf.runs);
    if (dwrite_factory_ == nullptr || text_length == 0 || delta_lines == 0) {
        return std::clamp(offset, 0, text_length);
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    LayoutBuildData data{};
    if (!Build_text_layout(dwrite_factory_, buf.base_style, buf.runs, font_families_,
                           format, layout, data)) {
        return std::clamp(offset, 0, text_length);
    }

    float caret_x = 0.0f;
    float caret_y = 0.0f;
    DWRITE_HIT_TEST_METRICS caret_metrics{};
    if (FAILED(layout->HitTestTextPosition(
            static_cast<UINT32>(std::clamp(offset, 0, text_length)), FALSE, &caret_x,
            &caret_y, &caret_metrics))) {
        return std::clamp(offset, 0, text_length);
    }

    UINT32 line_count = 0;
    (void)layout->GetLineMetrics(nullptr, 0, &line_count);
    if (line_count == 0) {
        return std::clamp(offset, 0, text_length);
    }

    std::vector<DWRITE_LINE_METRICS> line_metrics(line_count);
    if (FAILED(layout->GetLineMetrics(line_metrics.data(), line_count, &line_count))) {
        return std::clamp(offset, 0, text_length);
    }
    line_metrics.resize(line_count);

    std::vector<float> line_tops(line_metrics.size(), 0.0f);
    float current_top = 0.0f;
    for (size_t index = 0; index < line_metrics.size(); ++index) {
        line_tops[index] = current_top;
        current_top += line_metrics[index].height;
    }

    int32_t current_line = 0;
    for (size_t index = 0; index < line_metrics.size(); ++index) {
        float const line_bottom = line_tops[index] + line_metrics[index].height;
        if (caret_y < line_bottom || index + 1 == line_metrics.size()) {
            current_line = static_cast<int32_t>(index);
            break;
        }
    }

    int32_t const target_line = std::clamp(
        current_line + delta_lines, 0, static_cast<int32_t>(line_metrics.size()) - 1);
    float const target_y =
        line_tops[static_cast<size_t>(target_line)] +
        (line_metrics[static_cast<size_t>(target_line)].height * 0.5f);

    BOOL trailing_hit = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS hit_metrics{};
    if (FAILED(layout->HitTestPoint(static_cast<float>(preferred_x_px), target_y,
                                    &trailing_hit, &inside, &hit_metrics))) {
        return std::clamp(offset, 0, text_length);
    }
    (void)inside;

    return Insertion_offset_from_hit_test(hit_metrics, trailing_hit, text_length);
}

void D2DTextLayoutEngine::Rasterize(core::TextAnnotation &annotation) {
    annotation.bitmap_width_px = 0;
    annotation.bitmap_height_px = 0;
    annotation.bitmap_row_bytes = 0;
    annotation.premultiplied_bgra.clear();

    if (d2d_factory_ == nullptr || dwrite_factory_ == nullptr ||
        annotation.visual_bounds.Is_empty() || !Text_runs_have_text(annotation.runs)) {
        return;
    }

    ScopedCoInit const co_init = Ensure_com_initialized();
    (void)co_init;

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_factory));
    if (FAILED(hr) || !wic_factory) {
        return;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> wic_bitmap;
    hr = wic_factory->CreateBitmap(static_cast<UINT>(annotation.visual_bounds.Width()),
                                   static_cast<UINT>(annotation.visual_bounds.Height()),
                                   GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                   wic_bitmap.GetAddressOf());
    if (FAILED(hr) || !wic_bitmap) {
        return;
    }

    D2D1_RENDER_TARGET_PROPERTIES rt_props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        kDefaultDpi, kDefaultDpi);

    Microsoft::WRL::ComPtr<ID2D1RenderTarget> render_target;
    hr = d2d_factory_->CreateWicBitmapRenderTarget(wic_bitmap.Get(), rt_props,
                                                   render_target.GetAddressOf());
    if (FAILED(hr) || !render_target) {
        return;
    }
    render_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    LayoutBuildData data{};
    if (!Build_text_layout(dwrite_factory_, annotation.base_style, annotation.runs,
                           font_families_, format, layout, data)) {
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    hr = render_target->CreateSolidColorBrush(
        Colorref_to_d2d(annotation.base_style.color), brush.GetAddressOf());
    if (FAILED(hr) || !brush) {
        return;
    }

    float const draw_x =
        static_cast<float>(annotation.origin.x - annotation.visual_bounds.left);
    float const draw_y =
        static_cast<float>(annotation.origin.y - annotation.visual_bounds.top);

    render_target->BeginDraw();
    render_target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    render_target->DrawTextLayout(D2D1::Point2F(draw_x, draw_y), layout.Get(),
                                  brush.Get());
    hr = render_target->EndDraw();
    if (FAILED(hr)) {
        return;
    }

    WICRect rect = {0, 0, annotation.visual_bounds.Width(),
                    annotation.visual_bounds.Height()};
    annotation.bitmap_width_px = annotation.visual_bounds.Width();
    annotation.bitmap_height_px = annotation.visual_bounds.Height();
    annotation.bitmap_row_bytes = annotation.bitmap_width_px * 4;
    annotation.premultiplied_bgra.resize(
        static_cast<size_t>(annotation.bitmap_row_bytes) *
        static_cast<size_t>(annotation.bitmap_height_px));
    hr = wic_bitmap->CopyPixels(&rect, static_cast<UINT>(annotation.bitmap_row_bytes),
                                static_cast<UINT>(annotation.premultiplied_bgra.size()),
                                annotation.premultiplied_bgra.data());
    if (FAILED(hr)) {
        annotation.bitmap_width_px = 0;
        annotation.bitmap_height_px = 0;
        annotation.bitmap_row_bytes = 0;
        annotation.premultiplied_bgra.clear();
    }
}

void D2DTextLayoutEngine::Rasterize_bubble(core::BubbleAnnotation &annotation) {
    annotation.bitmap_width_px = 0;
    annotation.bitmap_height_px = 0;
    annotation.bitmap_row_bytes = 0;
    annotation.premultiplied_bgra.clear();

    if (d2d_factory_ == nullptr || dwrite_factory_ == nullptr ||
        annotation.diameter_px <= 0) {
        return;
    }

    int32_t const d = annotation.diameter_px;
    float const fd = static_cast<float>(d);

    ScopedCoInit const co_init = Ensure_com_initialized();
    (void)co_init;

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_factory));
    if (FAILED(hr) || !wic_factory) {
        return;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> wic_bitmap;
    hr = wic_factory->CreateBitmap(static_cast<UINT>(d), static_cast<UINT>(d),
                                   GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                   wic_bitmap.GetAddressOf());
    if (FAILED(hr) || !wic_bitmap) {
        return;
    }

    D2D1_RENDER_TARGET_PROPERTIES rt_props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        kDefaultDpi, kDefaultDpi);

    Microsoft::WRL::ComPtr<ID2D1RenderTarget> render_target;
    hr = d2d_factory_->CreateWicBitmapRenderTarget(wic_bitmap.Get(), rt_props,
                                                   render_target.GetAddressOf());
    if (FAILED(hr) || !render_target) {
        return;
    }

    COLORREF const text_color = core::Bubble_text_color(annotation.color);

    // Font size: fraction of diameter in DIP units. Larger fraction for 1-2 digits.
    int32_t const n = annotation.counter_value;
    float const font_fraction = (n >= 100) ? 0.38f : 0.55f;
    float const font_size_dip = font_fraction * fd;

    size_t const family_index = Text_font_choice_index(annotation.font_choice);
    std::wstring_view const family =
        font_families_[family_index].empty()
            ? Default_font_family(annotation.font_choice)
            : std::wstring_view(font_families_[family_index]);

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    hr = dwrite_factory_->CreateTextFormat(
        family.data(), nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, font_size_dip, L"", format.GetAddressOf());
    if (FAILED(hr) || !format) {
        return;
    }
    (void)format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    (void)format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    (void)format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    std::wstring const text = std::to_wstring(n);

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    hr = dwrite_factory_->CreateTextLayout(text.c_str(),
                                           static_cast<UINT32>(text.size()),
                                           format.Get(), fd, fd, layout.GetAddressOf());
    if (FAILED(hr) || !layout) {
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill_brush;
    hr = render_target->CreateSolidColorBrush(Colorref_to_d2d(annotation.color),
                                              fill_brush.GetAddressOf());
    if (FAILED(hr) || !fill_brush) {
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush;
    hr = render_target->CreateSolidColorBrush(Colorref_to_d2d(text_color),
                                              text_brush.GetAddressOf());
    if (FAILED(hr) || !text_brush) {
        return;
    }

    render_target->BeginDraw();
    render_target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // Filled circle (inset by 0.5px so the edge doesn't get clipped)
    float const r = fd / 2.0f;
    float const fill_r = r - 0.5f;
    if (fill_r > 0.0f) {
        D2D1_ELLIPSE const fill_ellipse =
            D2D1::Ellipse(D2D1::Point2F(r, r), fill_r, fill_r);
        render_target->FillEllipse(fill_ellipse, fill_brush.Get());
    }

    // Inner 1px stroke inset 1px from the outer edge (stroke center at r - 1.5)
    float const stroke_r = r - 1.5f;
    if (stroke_r > 0.0f) {
        D2D1_ELLIPSE const stroke_ellipse =
            D2D1::Ellipse(D2D1::Point2F(r, r), stroke_r, stroke_r);
        render_target->DrawEllipse(stroke_ellipse, text_brush.Get(), 1.0f);
    }

    // Number centered in the bitmap
    render_target->DrawTextLayout(D2D1::Point2F(0.0f, 0.0f), layout.Get(),
                                  text_brush.Get());

    hr = render_target->EndDraw();
    if (FAILED(hr)) {
        return;
    }

    WICRect rect = {0, 0, d, d};
    annotation.bitmap_width_px = d;
    annotation.bitmap_height_px = d;
    annotation.bitmap_row_bytes = d * 4;
    annotation.premultiplied_bgra.resize(
        static_cast<size_t>(annotation.bitmap_row_bytes) * static_cast<size_t>(d));
    hr = wic_bitmap->CopyPixels(&rect, static_cast<UINT>(annotation.bitmap_row_bytes),
                                static_cast<UINT>(annotation.premultiplied_bgra.size()),
                                annotation.premultiplied_bgra.data());
    if (FAILED(hr)) {
        annotation.bitmap_width_px = 0;
        annotation.bitmap_height_px = 0;
        annotation.bitmap_row_bytes = 0;
        annotation.premultiplied_bgra.clear();
    }
}

} // namespace greenflame
