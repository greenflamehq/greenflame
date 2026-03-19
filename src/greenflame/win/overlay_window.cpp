// Overlay window object: fullscreen capture/selection UI.

#include "win/overlay_window.h"

#include "app_config_store.h"
#include "greenflame_core/app_config.h"
#include "greenflame_core/modification_command.h"
#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/save_image_policy.h"
#include "greenflame_core/selection_handles.h"
#include "greenflame_core/snap_to_edges.h"
#include "greenflame_core/toolbar_placement.h"
#include "greenflame_core/window_query.h"
#include "win/d2d_overlay_resources.h"
#include "win/d2d_paint.h"
#include "win/display_queries.h"
#include "win/gdi_capture.h"
#include "win/save_image.h"
#include "win/ui_palette.h"

namespace {

constexpr wchar_t kOverlayWindowClass[] = L"GreenflameOverlay";
constexpr int32_t kSnapThresholdPx = 10;
constexpr int kToolbarButtonSizePx = 36;
constexpr int kToolbarButtonSeparatorPx = 9; // size / 4
constexpr int kAnnotationToolCursorResourceId = 102;
constexpr int kBrushToolGlyphResourceId = 103;
constexpr int kHighlighterToolGlyphResourceId = 104;
constexpr int kLineToolGlyphResourceId = 105;
constexpr int kArrowToolGlyphResourceId = 106;
constexpr int kRectangleToolGlyphResourceId = 107;
constexpr int kFilledRectangleToolGlyphResourceId = 108;
constexpr int kEllipseToolGlyphResourceId = 109;
constexpr int kFilledEllipseToolGlyphResourceId = 110;
constexpr int kHelpToolGlyphResourceId = 111;
constexpr int kTextToolGlyphResourceId = 112;
constexpr int kBubbleToolGlyphResourceId = 113;
constexpr UINT_PTR kBrushSizeOverlayTimerId = 1;
constexpr UINT_PTR kCaretBlinkTimerId = 2;
constexpr UINT_PTR kHighlighterStraightenTimerId = 3;

constexpr int kThumbnailMaxWidth = 320;
constexpr int kThumbnailMaxHeight = 120;
constexpr std::array<greenflame::core::TextFontChoice, 4> kTextWheelFontChoices = {
    {greenflame::core::TextFontChoice::Sans, greenflame::core::TextFontChoice::Serif,
     greenflame::core::TextFontChoice::Mono, greenflame::core::TextFontChoice::Art}};

[[nodiscard]] bool
Text_annotation_has_text(greenflame::core::TextAnnotation const &annotation) noexcept {
    for (greenflame::core::TextRun const &run : annotation.runs) {
        if (!run.text.empty()) {
            return true;
        }
    }
    return false;
}

struct ToolbarGlyphResourceSpec final {
    greenflame::OverlayToolbarGlyphId glyph = greenflame::OverlayToolbarGlyphId::None;
    int resource_id = 0;
};

constexpr std::array<ToolbarGlyphResourceSpec, 11> kToolbarGlyphResourceSpecs = {{
    {greenflame::OverlayToolbarGlyphId::Brush, kBrushToolGlyphResourceId},
    {greenflame::OverlayToolbarGlyphId::Highlighter, kHighlighterToolGlyphResourceId},
    {greenflame::OverlayToolbarGlyphId::Line, kLineToolGlyphResourceId},
    {greenflame::OverlayToolbarGlyphId::Arrow, kArrowToolGlyphResourceId},
    {greenflame::OverlayToolbarGlyphId::Rectangle, kRectangleToolGlyphResourceId},
    {greenflame::OverlayToolbarGlyphId::FilledRectangle,
     kFilledRectangleToolGlyphResourceId},
    {greenflame::OverlayToolbarGlyphId::Ellipse, kEllipseToolGlyphResourceId},
    {greenflame::OverlayToolbarGlyphId::FilledEllipse,
     kFilledEllipseToolGlyphResourceId},
    {greenflame::OverlayToolbarGlyphId::Text, kTextToolGlyphResourceId},
    {greenflame::OverlayToolbarGlyphId::Bubble, kBubbleToolGlyphResourceId},
    {greenflame::OverlayToolbarGlyphId::Help, kHelpToolGlyphResourceId},
}};

[[nodiscard]] HBITMAP
Create_thumbnail_from_capture(greenflame::GdiCaptureResult const &capture) {
    if (!capture.Is_valid()) {
        return nullptr;
    }
    return greenflame::Scale_bitmap_to_thumbnail(capture.bitmap, capture.width,
                                                 capture.height, kThumbnailMaxWidth,
                                                 kThumbnailMaxHeight);
}

[[nodiscard]] std::wstring Resolve_absolute_path(std::wstring_view path) {
    if (path.empty()) {
        return {};
    }

    std::wstring input(path);
    DWORD const required = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (required == 0) {
        return input;
    }

    std::wstring result(required, L'\0');
    DWORD const written =
        GetFullPathNameW(input.c_str(), required, result.data(), nullptr);
    if (written == 0) {
        return input;
    }
    if (written < result.size()) {
        result.resize(written);
    }
    return result;
}

[[nodiscard]] bool Copy_file_path_to_clipboard(std::wstring_view path,
                                               HWND owner_window) {
    std::wstring const absolute_path = Resolve_absolute_path(path);
    if (absolute_path.empty()) {
        return false;
    }

    size_t const path_chars = absolute_path.size();
    size_t const bytes = sizeof(DROPFILES) + (path_chars + 2) * sizeof(wchar_t);
    HGLOBAL file_list_memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (file_list_memory == nullptr) {
        return false;
    }

    void *const raw = GlobalLock(file_list_memory);
    if (raw == nullptr) {
        GlobalFree(file_list_memory);
        return false;
    }

    DROPFILES *dropfiles = static_cast<DROPFILES *>(raw);
    *dropfiles = {};
    dropfiles->pFiles = sizeof(DROPFILES);
    dropfiles->fWide = TRUE;

    std::vector<wchar_t> file_buf(path_chars + 2);
    std::span<wchar_t> files(file_buf);
    std::copy_n(absolute_path.c_str(), path_chars, files.data());
    files[path_chars] = L'\0';
    files[path_chars + 1] = L'\0';
    CLANG_WARN_IGNORE_PUSH("-Wunsafe-buffer-usage")
    wchar_t *const dest =
        reinterpret_cast<wchar_t *>(static_cast<uint8_t *>(raw) + dropfiles->pFiles);
    CLANG_WARN_IGNORE_POP()
    std::copy(file_buf.begin(), file_buf.end(), dest);
    GlobalUnlock(file_list_memory);

    bool copied = false;
    HWND const clipboard_owner =
        (owner_window != nullptr && IsWindow(owner_window) != 0) ? owner_window
                                                                 : nullptr;
    if (OpenClipboard(clipboard_owner) != 0) {
        if (EmptyClipboard() != 0 &&
            SetClipboardData(CF_HDROP, file_list_memory) != nullptr) {
            copied = true;
            file_list_memory = nullptr; // Clipboard owns memory after success.
        }
        CloseClipboard();
    }
    if (file_list_memory != nullptr) {
        GlobalFree(file_list_memory);
    }
    return copied;
}

[[nodiscard]] POINT To_point(greenflame::core::PointPx p) {
    POINT out{};
    out.x = p.x;
    out.y = p.y;
    return out;
}

[[nodiscard]] HCURSOR Cursor_for_handle(greenflame::core::SelectionHandle handle) {
    using Handle = greenflame::core::SelectionHandle;
    switch (handle) {
    case Handle::Top:
    case Handle::Bottom:
        return LoadCursorW(nullptr, IDC_SIZENS);
    case Handle::Left:
    case Handle::Right:
        return LoadCursorW(nullptr, IDC_SIZEWE);
    case Handle::TopLeft:
    case Handle::BottomRight:
        return LoadCursorW(nullptr, IDC_SIZENWSE);
    case Handle::TopRight:
    case Handle::BottomLeft:
        return LoadCursorW(nullptr, IDC_SIZENESW);
    }
    return LoadCursorW(nullptr, IDC_ARROW);
}

[[nodiscard]] HCURSOR Move_mode_cursor() { return LoadCursorW(nullptr, IDC_SIZEALL); }

[[nodiscard]] std::optional<greenflame::core::SelectionHandle>
Selection_handle_for_annotation_edit_target(
    greenflame::core::AnnotationEditTargetKind kind) noexcept {
    using Kind = greenflame::core::AnnotationEditTargetKind;
    using Handle = greenflame::core::SelectionHandle;

    switch (kind) {
    case Kind::RectangleTopLeftHandle:
        return Handle::TopLeft;
    case Kind::RectangleTopRightHandle:
        return Handle::TopRight;
    case Kind::RectangleBottomRightHandle:
        return Handle::BottomRight;
    case Kind::RectangleBottomLeftHandle:
        return Handle::BottomLeft;
    case Kind::RectangleTopHandle:
        return Handle::Top;
    case Kind::RectangleRightHandle:
        return Handle::Right;
    case Kind::RectangleBottomHandle:
        return Handle::Bottom;
    case Kind::RectangleLeftHandle:
        return Handle::Left;
    case Kind::Body:
    case Kind::LineStartHandle:
    case Kind::LineEndHandle:
    case Kind::FreehandStrokeStartHandle:
    case Kind::FreehandStrokeEndHandle:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<greenflame::core::SelectionHandle>
Selection_handle_for_active_annotation_edit(
    greenflame::core::AnnotationEditHandleKind kind) noexcept {
    using Kind = greenflame::core::AnnotationEditHandleKind;
    using Handle = greenflame::core::SelectionHandle;

    switch (kind) {
    case Kind::RectangleTopLeft:
        return Handle::TopLeft;
    case Kind::RectangleTopRight:
        return Handle::TopRight;
    case Kind::RectangleBottomRight:
        return Handle::BottomRight;
    case Kind::RectangleBottomLeft:
        return Handle::BottomLeft;
    case Kind::RectangleTop:
        return Handle::Top;
    case Kind::RectangleRight:
        return Handle::Right;
    case Kind::RectangleBottom:
        return Handle::Bottom;
    case Kind::RectangleLeft:
        return Handle::Left;
    case Kind::LineStart:
    case Kind::LineEnd:
    case Kind::FreehandStrokeStart:
    case Kind::FreehandStrokeEnd:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] HCURSOR Load_annotation_tool_cursor(HINSTANCE hinstance) {
    if (hinstance != nullptr) {
        HCURSOR const cursor =
            LoadCursorW(hinstance, MAKEINTRESOURCEW(kAnnotationToolCursorResourceId));
        if (cursor != nullptr) {
            return cursor;
        }
    }
    return LoadCursorW(nullptr, IDC_CROSS);
}

[[nodiscard]] std::optional<int32_t> Brush_width_delta_for_key(WPARAM wparam) noexcept {
    switch (wparam) {
    case VK_OEM_PLUS:
    case VK_ADD:
        return 1;
    case VK_OEM_MINUS:
    case VK_SUBTRACT:
        return -1;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::array<std::wstring_view, 4>
Resolve_text_font_families(greenflame::core::AppConfig const *config) noexcept {
    if (config == nullptr) {
        return greenflame::core::kDefaultTextFontFamilies;
    }
    return {{config->text_font_sans, config->text_font_serif, config->text_font_mono,
             config->text_font_art}};
}

[[nodiscard]] std::optional<greenflame::core::TextNavigationAction>
Text_navigation_action_for_key(WPARAM wparam, bool ctrl) noexcept {
    using Navigation = greenflame::core::TextNavigationAction;
    switch (wparam) {
    case VK_LEFT:
        return ctrl ? Navigation::WordLeft : Navigation::Left;
    case VK_RIGHT:
        return ctrl ? Navigation::WordRight : Navigation::Right;
    case VK_UP:
        return Navigation::Up;
    case VK_DOWN:
        return Navigation::Down;
    case VK_HOME:
        return ctrl ? Navigation::DocHome : Navigation::Home;
    case VK_END:
        return ctrl ? Navigation::DocEnd : Navigation::End;
    case VK_PRIOR:
        return Navigation::PgUp;
    case VK_NEXT:
        return Navigation::PgDn;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::wstring Normalize_text_for_clipboard(std::wstring_view text) {
    std::wstring normalized;
    normalized.reserve(text.size() * 2);

    for (size_t index = 0; index < text.size(); ++index) {
        wchar_t const ch = text[index];
        if (ch == L'\r') {
            normalized += L"\r\n";
            if (index + 1 < text.size() && text[index + 1] == L'\n') {
                ++index;
            }
            continue;
        }
        if (ch == L'\n') {
            normalized += L"\r\n";
            continue;
        }
        normalized.push_back(ch);
    }

    return normalized;
}

[[nodiscard]] std::wstring Normalize_text_from_clipboard(std::wstring_view text) {
    std::wstring normalized;
    normalized.reserve(text.size());

    for (size_t index = 0; index < text.size(); ++index) {
        wchar_t const ch = text[index];
        if (ch == L'\r') {
            normalized.push_back(L'\n');
            if (index + 1 < text.size() && text[index + 1] == L'\n') {
                ++index;
            }
            continue;
        }
        normalized.push_back(ch);
    }

    return normalized;
}

template <typename T> struct ComPtr {
    T *p = nullptr;

    ComPtr() = default;
    ~ComPtr() {
        if (p != nullptr) {
            p->Release();
        }
    }

    ComPtr(ComPtr const &) = delete;
    ComPtr &operator=(ComPtr const &) = delete;

    T **operator&() { return &p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct CoInitGuard {
    bool owned = false;

    explicit CoInitGuard(bool owns) : owned(owns) {}
    ~CoInitGuard() {
        if (owned) {
            CoUninitialize();
        }
    }

    CoInitGuard(CoInitGuard const &) = delete;
    CoInitGuard &operator=(CoInitGuard const &) = delete;
};

[[nodiscard]] std::shared_ptr<greenflame::OverlayButtonGlyph>
Load_png_resource_alpha_mask(HINSTANCE hinstance, int resource_id) {
    if (hinstance == nullptr) {
        return {};
    }

    HRSRC const resource_info =
        FindResourceW(hinstance, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (resource_info == nullptr) {
        return {};
    }
    DWORD const resource_size = SizeofResource(hinstance, resource_info);
    if (resource_size == 0) {
        return {};
    }
    HGLOBAL const resource_handle = LoadResource(hinstance, resource_info);
    if (resource_handle == nullptr) {
        return {};
    }
    void const *const resource_data = LockResource(resource_handle);
    if (resource_data == nullptr) {
        return {};
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coinit = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        coinit = false;
    } else if (FAILED(hr)) {
        return {};
    }
    CoInitGuard const co_guard(coinit);

    ComPtr<IWICImagingFactory> factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        return {};
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream) {
        return {};
    }

    BYTE *const stream_bytes =
        const_cast<BYTE *>(static_cast<BYTE const *>(resource_data));
    hr = stream->InitializeFromMemory(stream_bytes, resource_size);
    if (FAILED(hr)) {
        return {};
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream.p, nullptr,
                                          WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) {
        return {};
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        return {};
    }

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        return {};
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        return {};
    }
    hr = converter->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return {};
    }

    UINT const stride = width * 4;
    size_t const buffer_size =
        static_cast<size_t>(stride) * static_cast<size_t>(height);
    std::vector<uint8_t> pixels(buffer_size);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()),
                               pixels.data());
    if (FAILED(hr)) {
        return {};
    }

    auto glyph = std::make_shared<greenflame::OverlayButtonGlyph>();
    glyph->width = static_cast<int>(width);
    glyph->height = static_cast<int>(height);
    glyph->alpha_mask.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t i = 0; i < glyph->alpha_mask.size(); ++i) {
        glyph->alpha_mask[i] = pixels[i * 4 + 3];
    }
    if (!glyph->Is_valid()) {
        return {};
    }
    return glyph;
}

} // namespace

namespace greenflame {

struct OverlayWindow::OverlayResources {
    GdiCaptureResult capture = {};
    std::array<std::shared_ptr<OverlayButtonGlyph const>,
               static_cast<size_t>(OverlayToolbarGlyphId::Count)>
        toolbar_glyphs = {};

    OverlayResources() = default;
    ~OverlayResources() { Reset(); }

    OverlayResources(OverlayResources const &) = delete;
    OverlayResources &operator=(OverlayResources const &) = delete;

    [[nodiscard]] bool Initialize_for_capture(HINSTANCE hinstance) {
        if (!capture.Is_valid()) {
            return false;
        }
        for (ToolbarGlyphResourceSpec const &spec : kToolbarGlyphResourceSpecs) {
            toolbar_glyphs[Overlay_toolbar_glyph_index(spec.glyph)] =
                Load_png_resource_alpha_mask(hinstance, spec.resource_id);
        }
        return true;
    }

    [[nodiscard]] std::array<OverlayButtonGlyph const *,
                             static_cast<size_t>(OverlayToolbarGlyphId::Count)>
    Glyph_pointers() const noexcept {
        std::array<OverlayButtonGlyph const *,
                   static_cast<size_t>(OverlayToolbarGlyphId::Count)>
            glyphs{};
        for (size_t i = 0; i < toolbar_glyphs.size(); ++i) {
            glyphs[i] = toolbar_glyphs[i].get();
        }
        return glyphs;
    }

    void Reset() noexcept {
        capture.Free();
        for (auto &glyph : toolbar_glyphs) {
            glyph.reset();
        }
    }
};

OverlayWindow::OverlayWindow(IOverlayEvents *events, core::AppConfig *config,
                             IWindowQuery *window_query)
    : events_(events), config_(config), window_query_(window_query),
      resources_(std::make_unique<OverlayResources>()) {}

OverlayWindow::~OverlayWindow() { Destroy(); }

void OverlayWindow::Set_hotkey_help_content(
    core::OverlayHelpContent const *content) noexcept {
    hotkey_help_overlay_.Set_content(content);
}

void OverlayWindow::Set_testing_toolbar(bool enable) noexcept {
    testing_toolbar_ = enable;
}

void OverlayWindow::Show_help_overlay_at_cursor() {
    if (!Is_selection_stable_for_help()) {
        return;
    }

    RECT overlay_rect{};
    if (GetWindowRect(hwnd_, &overlay_rect) == 0) {
        return;
    }

    core::RectPx const overlay_screen_rect = core::RectPx::From_ltrb(
        static_cast<int32_t>(overlay_rect.left), static_cast<int32_t>(overlay_rect.top),
        static_cast<int32_t>(overlay_rect.right),
        static_cast<int32_t>(overlay_rect.bottom));
    hotkey_help_overlay_.Show_at_cursor(
        Get_cursor_pos_px(), controller_.State().cached_monitors, overlay_screen_rect);
    (void)Clear_toolbar_hover_states();
    Refresh_cursor();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void OverlayWindow::Hide_help_overlay(bool suppress_next_lbutton_up) {
    if (!hotkey_help_overlay_.Is_visible()) {
        return;
    }

    hotkey_help_overlay_.Hide();
    if (suppress_next_lbutton_up) {
        suppress_next_lbutton_up_ = true;
    }
    (void)Refresh_hover_handle();
    (void)Update_toolbar_hover_states(Get_client_cursor_pos_px(hwnd_));
    Refresh_cursor();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

std::vector<core::PointPx>
OverlayWindow::Compute_toolbar_positions(int button_count) const {
    auto const &s = controller_.State();
    if (s.final_selection.Is_empty() || button_count <= 0) {
        return {};
    }

    // Build client-space monitor rects.
    std::vector<core::RectPx> monitor_client_rects;
    monitor_client_rects.reserve(s.cached_monitors.size());
    RECT overlay_rect{};
    if (GetWindowRect(hwnd_, &overlay_rect) != 0) {
        for (auto const &monitor : s.cached_monitors) {
            monitor_client_rects.push_back(core::RectPx::From_ltrb(
                monitor.bounds.left - static_cast<int32_t>(overlay_rect.left),
                monitor.bounds.top - static_cast<int32_t>(overlay_rect.top),
                monitor.bounds.right - static_cast<int32_t>(overlay_rect.left),
                monitor.bounds.bottom - static_cast<int32_t>(overlay_rect.top)));
        }
    }

    core::ToolbarPlacementParams params{};
    params.selection = s.final_selection;
    params.available = monitor_client_rects;
    params.button_size = kToolbarButtonSizePx;
    params.separator = kToolbarButtonSeparatorPx;
    params.button_count = button_count;
    return core::Compute_toolbar_placement(params).positions;
}

OverlayButtonGlyph const *OverlayWindow::Resolve_toolbar_button_glyph(
    OverlayToolbarGlyphId glyph) const noexcept {
    if (resources_ == nullptr) {
        return nullptr;
    }
    size_t const index = Overlay_toolbar_glyph_index(glyph);
    if (index >= resources_->toolbar_glyphs.size() ||
        glyph == OverlayToolbarGlyphId::None) {
        return nullptr;
    }
    return resources_->toolbar_glyphs[index].get();
}

void OverlayWindow::Rebuild_toolbar_buttons() {
    if (!controller_.Should_show_annotation_toolbar()) {
        (void)Clear_toolbar_hover_states();
        toolbar_buttons_.clear();
        return;
    }

    std::vector<core::AnnotationToolbarButtonView> const views =
        controller_.Build_annotation_toolbar_button_views();
    auto const map_annotation_glyph = [](core::AnnotationToolbarGlyph glyph) noexcept {
        switch (glyph) {
        case core::AnnotationToolbarGlyph::Brush:
            return OverlayToolbarGlyphId::Brush;
        case core::AnnotationToolbarGlyph::Highlighter:
            return OverlayToolbarGlyphId::Highlighter;
        case core::AnnotationToolbarGlyph::Line:
            return OverlayToolbarGlyphId::Line;
        case core::AnnotationToolbarGlyph::Arrow:
            return OverlayToolbarGlyphId::Arrow;
        case core::AnnotationToolbarGlyph::Rectangle:
            return OverlayToolbarGlyphId::Rectangle;
        case core::AnnotationToolbarGlyph::FilledRectangle:
            return OverlayToolbarGlyphId::FilledRectangle;
        case core::AnnotationToolbarGlyph::Ellipse:
            return OverlayToolbarGlyphId::Ellipse;
        case core::AnnotationToolbarGlyph::FilledEllipse:
            return OverlayToolbarGlyphId::FilledEllipse;
        case core::AnnotationToolbarGlyph::Text:
            return OverlayToolbarGlyphId::Text;
        case core::AnnotationToolbarGlyph::Bubble:
            return OverlayToolbarGlyphId::Bubble;
        case core::AnnotationToolbarGlyph::None:
            return OverlayToolbarGlyphId::None;
        }
        return OverlayToolbarGlyphId::None;
    };

    size_t const spacer_count = views.empty() ? 0u : 1u;
    size_t const trailing_button_count = 1u; // Help button.
    std::vector<ToolbarLayoutItem> layout_items;
    layout_items.reserve(views.size() + spacer_count + trailing_button_count);
    for (auto const &view : views) {
        ToolbarButtonModel model{};
        model.action = ToolbarButtonAction::SelectAnnotationTool;
        model.tool_id = view.id;
        model.glyph = map_annotation_glyph(view.glyph);
        model.tooltip = view.tooltip;
        model.label = view.label;
        model.active = view.active;
        layout_items.push_back(
            ToolbarLayoutItem{ToolbarLayoutItemKind::Button, std::move(model)});
    }
    if (!views.empty()) {
        layout_items.push_back(ToolbarLayoutItem{ToolbarLayoutItemKind::Spacer});
    }
    {
        ToolbarButtonModel help_model{};
        help_model.action = ToolbarButtonAction::ShowHelp;
        help_model.glyph = OverlayToolbarGlyphId::Help;
        help_model.tooltip = L"Help";
        help_model.label = L"?";
        layout_items.push_back(
            ToolbarLayoutItem{ToolbarLayoutItemKind::Button, std::move(help_model)});
    }

    std::vector<core::PointPx> const positions =
        Compute_toolbar_positions(static_cast<int>(layout_items.size()));

    toolbar_buttons_.clear();
    toolbar_buttons_.reserve(positions.size());
    size_t const item_count = std::min(layout_items.size(), positions.size());
    for (size_t i = 0; i < item_count; ++i) {
        ToolbarLayoutItem const &layout_item = layout_items[i];
        if (layout_item.kind != ToolbarLayoutItemKind::Button ||
            !layout_item.button.has_value()) {
            continue;
        }
        ToolbarButtonModel const &model = *layout_item.button;
        ToolbarButtonEntry entry{};
        entry.action = model.action;
        entry.tool_id = model.tool_id;
        entry.glyph = model.glyph;
        entry.tooltip = model.tooltip;
        OverlayButtonGlyph const *const glyph =
            Resolve_toolbar_button_glyph(entry.glyph);
        if (glyph != nullptr) {
            entry.button = std::make_unique<OverlayButton>(
                positions[i], kToolbarButtonSizePx, glyph, false, model.active);
        } else {
            entry.button = std::make_unique<OverlayButton>(
                positions[i], kToolbarButtonSizePx, model.label, false, model.active);
        }
        toolbar_buttons_.push_back(std::move(entry));
    }
}

bool OverlayWindow::Register_window_class(HINSTANCE hinstance) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = 0; // CS_HREDRAW/CS_VREDRAW cause DWM flicker with D2D
    window_class.lpfnWndProc = &OverlayWindow::Static_wnd_proc;
    window_class.hInstance = hinstance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = kOverlayWindowClass;
    return RegisterClassExW(&window_class) != 0;
}

bool OverlayWindow::Create_and_show(HINSTANCE hinstance) {
    if (Is_open()) {
        return true;
    }
    if (window_query_ == nullptr) {
        return false;
    }
    hinstance_ = hinstance;
    resources_->Reset();
    mouse_wheel_delta_remainder_ = 0;
    transient_center_label_text_.clear();
    caret_blink_visible_ = true;
    suppress_next_lbutton_up_ = false;
    color_wheel_ = {};
    text_layout_engine_.reset();

    core::RectPx const bounds = Get_virtual_desktop_bounds_px();
    HWND const hwnd = CreateWindowExW(
        WS_EX_TOPMOST, kOverlayWindowClass, L"", WS_POPUP, bounds.left, bounds.top,
        bounds.Width(), bounds.Height(), nullptr, nullptr, hinstance_, this);
    if (!hwnd) {
        hinstance_ = nullptr;
        return false;
    }

    if (!Capture_virtual_desktop(resources_->capture) ||
        !resources_->Initialize_for_capture(hinstance_)) {
        DestroyWindow(hwnd);
        return false;
    }

    // Initialize D2D render pipeline.
    d2d_resources_ = std::make_unique<D2DOverlayResources>();
    if (!d2d_resources_->Initialize_factory() ||
        !d2d_resources_->Create_hwnd_rt(hwnd, resources_->capture.width,
                                        resources_->capture.height) ||
        !d2d_resources_->Upload_screenshot(resources_->capture) ||
        !d2d_resources_->Create_shared_resources() ||
        !d2d_resources_->Create_cache_targets(resources_->capture.width,
                                              resources_->capture.height)) {
        d2d_resources_.reset();
    } else {
        auto const glyphs = resources_->Glyph_pointers();
        (void)d2d_resources_->Upload_glyph_bitmaps(
            std::span<OverlayButtonGlyph const *const>(glyphs));
        if (d2d_resources_->factory && d2d_resources_->dwrite_factory) {
            text_layout_engine_ = std::make_unique<D2DTextLayoutEngine>(
                d2d_resources_->factory.Get(), d2d_resources_->dwrite_factory.Get());
            text_layout_engine_->Set_font_families(Resolve_text_font_families(config_));
        }
    }

    controller_.Reset_for_session(Get_monitors_with_bounds());
    controller_.Set_text_layout_engine(text_layout_engine_.get());
    controller_.Refresh_snap_edges(Collect_visible_snap_edges(), bounds.left,
                                   bounds.top);
    auto const tool_step = [&](core::AnnotationToolId tool, int32_t step) {
        controller_.Set_tool_size_step(tool, step);
    };
    if (config_ != nullptr) {
        tool_step(core::AnnotationToolId::Freehand, config_->freehand_size);
        tool_step(core::AnnotationToolId::Line, config_->line_size);
        tool_step(core::AnnotationToolId::Arrow, config_->arrow_size);
        tool_step(core::AnnotationToolId::Rectangle, config_->rect_size);
        tool_step(core::AnnotationToolId::Ellipse, config_->ellipse_size);
        tool_step(core::AnnotationToolId::Highlighter, config_->highlighter_size);
        tool_step(core::AnnotationToolId::Bubble, config_->bubble_size);
        tool_step(core::AnnotationToolId::Text, config_->text_size);
    } else {
        tool_step(core::AnnotationToolId::Freehand,
                  core::AppConfig::kDefaultFreehandSize);
        tool_step(core::AnnotationToolId::Line, core::AppConfig::kDefaultLineSize);
        tool_step(core::AnnotationToolId::Arrow, core::AppConfig::kDefaultArrowSize);
        tool_step(core::AnnotationToolId::Rectangle, core::AppConfig::kDefaultRectSize);
        tool_step(core::AnnotationToolId::Ellipse,
                  core::AppConfig::kDefaultEllipseSize);
        tool_step(core::AnnotationToolId::Highlighter,
                  core::AppConfig::kDefaultHighlighterSize);
        tool_step(core::AnnotationToolId::Bubble, core::AppConfig::kDefaultBubbleSize);
        tool_step(core::AnnotationToolId::Text, core::AppConfig::kDefaultTextSize);
    }
    controller_.Set_brush_annotation_color(
        config_ != nullptr ? config_->annotation_colors[static_cast<size_t>(
                                 core::Clamp_annotation_color_index(
                                     config_->current_annotation_color_index))]
                           : core::kDefaultAnnotationColorPalette[static_cast<size_t>(
                                 core::kDefaultAnnotationColorIndex)]);
    controller_.Set_highlighter_color(
        config_ != nullptr ? config_->highlighter_colors[static_cast<size_t>(
                                 core::Clamp_highlighter_color_index(
                                     config_->current_highlighter_color_index))]
                           : core::kDefaultHighlighterColorPalette[static_cast<size_t>(
                                 core::kDefaultHighlighterColorIndex)]);
    controller_.Set_highlighter_opacity_percent(
        config_ != nullptr ? config_->highlighter_opacity_percent
                           : core::kDefaultHighlighterOpacityPercent);
    controller_.Set_text_current_font(config_ != nullptr ? config_->text_current_font
                                                         : core::TextFontChoice::Sans);
    controller_.Set_bubble_current_font(
        config_ != nullptr ? config_->bubble_current_font : core::TextFontChoice::Sans);
    ShowWindow(hwnd, SW_SHOW);
    return true;
}

void OverlayWindow::Destroy() {
    if (!Is_open()) {
        return;
    }
    DestroyWindow(hwnd_);
}

bool OverlayWindow::Is_open() const { return hwnd_ != nullptr && IsWindow(hwnd_) != 0; }

core::SnapEdges OverlayWindow::Collect_visible_snap_edges() const {
    core::SnapEdges snap_edges;
    if (window_query_ != nullptr && hwnd_ != nullptr) {
        window_query_->Get_visible_top_level_window_snap_edges(hwnd_, snap_edges);
    }
    std::vector<core::RectPx> monitor_rects;
    monitor_rects.reserve(controller_.State().cached_monitors.size());
    for (auto const &monitor : controller_.State().cached_monitors) {
        monitor_rects.push_back(monitor.bounds);
    }
    core::SnapEdges const monitor_edges =
        core::Build_snap_edges_from_screen_rects(monitor_rects, 0, 0);
    snap_edges.vertical.insert(snap_edges.vertical.end(),
                               monitor_edges.vertical.begin(),
                               monitor_edges.vertical.end());
    snap_edges.horizontal.insert(snap_edges.horizontal.end(),
                                 monitor_edges.horizontal.begin(),
                                 monitor_edges.horizontal.end());
    return snap_edges;
}

bool OverlayWindow::Handle_tool_size_delta(int32_t delta_steps) {
    std::optional<core::AnnotationToolId> const active_tool =
        controller_.Active_annotation_tool();
    if (!active_tool.has_value()) {
        return false;
    }
    int32_t const step = controller_.Tool_size_step(*active_tool);
    // step == 0 means this tool does not have a size.
    // Also skip when there's no selection or a text edit is blocking input.
    if (step == 0 || controller_.State().final_selection.Is_empty() ||
        (*active_tool == core::AnnotationToolId::Text &&
         controller_.Has_active_text_edit())) {
        return false;
    }
    bool const changed = controller_.Adjust_tool_size(delta_steps).has_value();
    if (changed && config_ != nullptr) {
        int32_t const new_step = controller_.Tool_size_step(*active_tool);
        switch (*active_tool) {
        case core::AnnotationToolId::Freehand:
            config_->freehand_size = new_step;
            break;
        case core::AnnotationToolId::Line:
            config_->line_size = new_step;
            break;
        case core::AnnotationToolId::Arrow:
            config_->arrow_size = new_step;
            break;
        case core::AnnotationToolId::Rectangle:
            config_->rect_size = new_step;
            break;
        case core::AnnotationToolId::Ellipse:
            config_->ellipse_size = new_step;
            break;
        case core::AnnotationToolId::Highlighter:
            config_->highlighter_size = new_step;
            break;
        case core::AnnotationToolId::Bubble:
            config_->bubble_size = new_step;
            break;
        case core::AnnotationToolId::Text:
            config_->text_size = new_step;
            break;
        case core::AnnotationToolId::FilledRectangle:
        case core::AnnotationToolId::FilledEllipse:
            break;
        }
        config_->Normalize();
        (void)Save_app_config(*config_);
    }
    Show_tool_size_overlay(controller_.Tool_size_step(*active_tool));
    return true;
}

void OverlayWindow::Show_tool_size_overlay(int32_t step) {
    int32_t const duration_ms =
        (config_ != nullptr) ? config_->tool_size_overlay_duration_ms
                             : core::AppConfig::kDefaultToolSizeOverlayDurationMs;
    if (duration_ms <= 0) {
        Clear_transient_center_label(true);
        return;
    }

    transient_center_label_text_ = std::to_wstring(step);
    (void)SetTimer(hwnd_, kBrushSizeOverlayTimerId, static_cast<UINT>(duration_ms),
                   nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::Clear_transient_center_label(bool repaint) {
    if (hwnd_ != nullptr) {
        (void)KillTimer(hwnd_, kBrushSizeOverlayTimerId);
    }
    if (transient_center_label_text_.empty()) {
        return;
    }
    transient_center_label_text_.clear();
    if (repaint && hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool OverlayWindow::Read_clipboard_text(std::wstring &out) const {
    out.clear();
    HWND const clipboard_owner =
        (hwnd_ != nullptr && IsWindow(hwnd_) != 0) ? hwnd_ : nullptr;
    if (OpenClipboard(clipboard_owner) == 0) {
        return false;
    }

    bool read_text = false;
    HGLOBAL clipboard_data = nullptr;
    do {
        if (IsClipboardFormatAvailable(CF_UNICODETEXT) == 0) {
            break;
        }
        clipboard_data = GetClipboardData(CF_UNICODETEXT);
        if (clipboard_data == nullptr) {
            break;
        }
        void const *const raw = GlobalLock(clipboard_data);
        if (raw == nullptr) {
            break;
        }

        size_t const wchar_capacity = GlobalSize(clipboard_data) / sizeof(wchar_t);
        std::vector<wchar_t> chars(wchar_capacity);
        std::copy_n(static_cast<wchar_t const *>(raw), wchar_capacity, chars.begin());
        auto const terminator = std::find(chars.begin(), chars.end(), L'\0');
        out = Normalize_text_from_clipboard(std::wstring_view(
            chars.data(),
            static_cast<size_t>(std::distance(chars.begin(), terminator))));
        GlobalUnlock(clipboard_data);
        read_text = true;
    } while (false);

    CloseClipboard();
    return read_text;
}

void OverlayWindow::Write_clipboard_text(std::wstring_view text) const {
    if (text.empty()) {
        return;
    }

    std::wstring const clipboard_text = Normalize_text_for_clipboard(text);
    size_t const bytes = (clipboard_text.size() + 1) * sizeof(wchar_t);
    HGLOBAL clipboard_data = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (clipboard_data == nullptr) {
        return;
    }

    void *const raw = GlobalLock(clipboard_data);
    if (raw == nullptr) {
        GlobalFree(clipboard_data);
        return;
    }

    std::copy_n(clipboard_text.c_str(), clipboard_text.size() + 1,
                static_cast<wchar_t *>(raw));
    GlobalUnlock(clipboard_data);

    HWND const clipboard_owner =
        (hwnd_ != nullptr && IsWindow(hwnd_) != 0) ? hwnd_ : nullptr;
    if (OpenClipboard(clipboard_owner) == 0) {
        GlobalFree(clipboard_data);
        return;
    }

    if (EmptyClipboard() != 0 &&
        SetClipboardData(CF_UNICODETEXT, clipboard_data) != nullptr) {
        clipboard_data = nullptr;
    }
    CloseClipboard();
    if (clipboard_data != nullptr) {
        GlobalFree(clipboard_data);
    }
}

void OverlayWindow::Cancel_highlighter_straighten_pending() noexcept {
    if (highlighter_straighten_pending_) {
        (void)KillTimer(hwnd_, kHighlighterStraightenTimerId);
        highlighter_straighten_pending_ = false;
    }
}

void OverlayWindow::Reset_caret_blink() {
    caret_blink_visible_ = true;
    if (hwnd_ == nullptr || !controller_.Has_active_text_edit()) {
        if (hwnd_ != nullptr) {
            (void)KillTimer(hwnd_, kCaretBlinkTimerId);
        }
        return;
    }

    UINT const interval = GetCaretBlinkTime();
    if (interval == 0 || interval == INFINITE) {
        (void)KillTimer(hwnd_, kCaretBlinkTimerId);
        return;
    }
    (void)SetTimer(hwnd_, kCaretBlinkTimerId, interval, nullptr);
}

bool OverlayWindow::Can_show_color_wheel() const noexcept {
    auto const &s = controller_.State();
    return controller_.Active_annotation_tool().has_value() &&
           !s.final_selection.Is_empty() && !s.dragging && !s.handle_dragging &&
           !s.move_dragging && !s.modifier_preview &&
           !controller_.Has_active_annotation_gesture() &&
           !hotkey_help_overlay_.Is_visible();
}

std::span<const COLORREF> OverlayWindow::Current_tool_color_palette() const noexcept {
    if (controller_.Active_annotation_tool() == core::AnnotationToolId::Highlighter) {
        if (config_ != nullptr) {
            return std::span<const COLORREF>(config_->highlighter_colors);
        }
        return std::span<const COLORREF>(core::kDefaultHighlighterColorPalette);
    }
    if (config_ != nullptr) {
        return std::span<const COLORREF>(config_->annotation_colors);
    }
    return std::span<const COLORREF>(core::kDefaultAnnotationColorPalette);
}

size_t OverlayWindow::Current_annotation_color_index() const noexcept {
    if (controller_.Active_annotation_tool() == core::AnnotationToolId::Highlighter) {
        if (config_ != nullptr) {
            return static_cast<size_t>(core::Clamp_highlighter_color_index(
                config_->current_highlighter_color_index));
        }
        return static_cast<size_t>(core::kDefaultHighlighterColorIndex);
    }
    if (config_ != nullptr) {
        return static_cast<size_t>(core::Clamp_annotation_color_index(
            config_->current_annotation_color_index));
    }
    return static_cast<size_t>(core::kDefaultAnnotationColorIndex);
}

size_t OverlayWindow::Current_color_wheel_segment_count() const noexcept {
    auto const tool = controller_.Active_annotation_tool();
    if (tool == core::AnnotationToolId::Text ||
        tool == core::AnnotationToolId::Bubble) {
        return (color_wheel_.text_mode == core::TextWheelMode::Color)
                   ? core::kAnnotationColorSlotCount
                   : kTextWheelFontChoices.size();
    }
    return Current_tool_color_palette().size();
}

void OverlayWindow::Show_color_wheel(core::PointPx center) {
    if (!Can_show_color_wheel()) {
        return;
    }
    Clear_transient_center_label(false);
    color_wheel_.visible = true;
    color_wheel_.center = center;
    color_wheel_.hovered_segment = std::nullopt;
    color_wheel_.hovered_hub = std::nullopt;
    last_hover_handle_ = std::nullopt;
    (void)Clear_toolbar_hover_states();
    Refresh_cursor();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::Dismiss_color_wheel(bool repaint) {
    if (!color_wheel_.visible) {
        return;
    }
    core::TextWheelMode const saved_mode = color_wheel_.text_mode;
    color_wheel_ = {};
    color_wheel_.text_mode = saved_mode;
    if (hwnd_ != nullptr) {
        Refresh_cursor();
    }
    if (repaint && hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool OverlayWindow::Update_color_wheel_hover(core::PointPx cursor) {
    if (!color_wheel_.visible) {
        return false;
    }
    auto const active_tool = controller_.Active_annotation_tool();
    bool const is_style_hub = active_tool == core::AnnotationToolId::Text ||
                              active_tool == core::AnnotationToolId::Bubble;
    if (is_style_hub) {
        auto hub_hit = core::Hit_test_text_wheel_hub(color_wheel_.center, cursor);
        if (hub_hit.has_value()) {
            bool const changed = (hub_hit != color_wheel_.hovered_hub ||
                                  color_wheel_.hovered_segment.has_value());
            color_wheel_.hovered_hub = hub_hit;
            color_wheel_.hovered_segment = std::nullopt;
            return changed;
        }
        bool changed = color_wheel_.hovered_hub.has_value();
        color_wheel_.hovered_hub = std::nullopt;
        std::optional<size_t> const ring = core::Hit_test_color_wheel_segment(
            color_wheel_.center, cursor, Current_color_wheel_segment_count());
        if (ring != color_wheel_.hovered_segment) {
            color_wheel_.hovered_segment = ring;
            changed = true;
        }
        return changed;
    }
    // Non-style-hub: original behavior.
    std::optional<size_t> const hovered = core::Hit_test_color_wheel_segment(
        color_wheel_.center, cursor, Current_color_wheel_segment_count());
    if (hovered == color_wheel_.hovered_segment) {
        return false;
    }
    color_wheel_.hovered_segment = hovered;
    return true;
}

void OverlayWindow::Select_color_wheel_segment(size_t index) {
    std::span<const COLORREF> const palette = Current_tool_color_palette();
    auto const active_tool = controller_.Active_annotation_tool();
    if (active_tool == core::AnnotationToolId::Text ||
        active_tool == core::AnnotationToolId::Bubble) {
        if (color_wheel_.text_mode == core::TextWheelMode::Color) {
            if (index < palette.size()) {
                controller_.Set_brush_annotation_color(palette[index]);
                if (config_ != nullptr) {
                    config_->current_annotation_color_index =
                        static_cast<int32_t>(index);
                    config_->Normalize();
                    (void)Save_app_config(*config_);
                }
            }
        } else {
            if (index < kTextWheelFontChoices.size()) {
                if (active_tool == core::AnnotationToolId::Text) {
                    controller_.Set_text_current_font(kTextWheelFontChoices[index]);
                    if (config_ != nullptr) {
                        config_->text_current_font = kTextWheelFontChoices[index];
                        config_->Normalize();
                        (void)Save_app_config(*config_);
                    }
                } else {
                    controller_.Set_bubble_current_font(kTextWheelFontChoices[index]);
                    if (config_ != nullptr) {
                        config_->bubble_current_font = kTextWheelFontChoices[index];
                        config_->Normalize();
                        (void)Save_app_config(*config_);
                    }
                }
            }
        }
        return;
    }

    if (index >= palette.size()) {
        return;
    }

    if (controller_.Active_annotation_tool() == core::AnnotationToolId::Highlighter) {
        controller_.Set_highlighter_color(palette[index]);
        if (config_ != nullptr) {
            config_->current_highlighter_color_index = static_cast<int32_t>(index);
            config_->Normalize();
            (void)Save_app_config(*config_);
        }
        return;
    }

    controller_.Set_brush_annotation_color(palette[index]);
    if (config_ != nullptr) {
        config_->current_annotation_color_index = static_cast<int32_t>(index);
        config_->Normalize();
        (void)Save_app_config(*config_);
    }
}

bool OverlayWindow::Clear_toolbar_hover_states() {
    bool changed = false;
    for (auto const &entry : toolbar_buttons_) {
        if (entry.button != nullptr && entry.button->Is_hovered()) {
            entry.button->On_mouse_leave();
            changed = true;
        }
    }
    return changed;
}

bool OverlayWindow::Update_toolbar_hover_states(core::PointPx cursor) {
    if (!controller_.Can_interact_with_annotation_toolbar() ||
        toolbar_buttons_.empty()) {
        return Clear_toolbar_hover_states();
    }

    bool changed = false;
    for (auto const &entry : toolbar_buttons_) {
        if (entry.button == nullptr) {
            continue;
        }
        bool const was_hovered = entry.button->Is_hovered();
        bool const is_hovered = entry.button->Hit_test(cursor);
        if (was_hovered == is_hovered) {
            continue;
        }
        if (is_hovered) {
            entry.button->On_mouse_enter();
        } else {
            entry.button->On_mouse_leave();
        }
        changed = true;
    }
    return changed;
}

bool OverlayWindow::Should_show_brush_cursor_preview() const {
    if (color_wheel_.visible) {
        return false;
    }
    auto const &s = controller_.State();
    auto const tool = controller_.Active_annotation_tool();
    return (tool == core::AnnotationToolId::Freehand ||
            tool == core::AnnotationToolId::Bubble) &&
           !s.final_selection.Is_empty() && !s.dragging && !s.handle_dragging &&
           !s.move_dragging && !s.modifier_preview && !last_hover_handle_.has_value() &&
           !controller_.Has_active_annotation_gesture();
}

bool OverlayWindow::Should_show_square_cursor_preview() const {
    if (color_wheel_.visible) {
        return false;
    }
    auto const &s = controller_.State();
    std::optional<core::AnnotationToolId> const active_tool =
        controller_.Active_annotation_tool();
    return active_tool.has_value() &&
           (*active_tool == core::AnnotationToolId::Highlighter ||
            *active_tool == core::AnnotationToolId::Line ||
            *active_tool == core::AnnotationToolId::Arrow ||
            *active_tool == core::AnnotationToolId::Rectangle ||
            *active_tool == core::AnnotationToolId::Ellipse) &&
           !s.final_selection.Is_empty() && !s.dragging && !s.handle_dragging &&
           !s.move_dragging && !s.modifier_preview && !last_hover_handle_.has_value() &&
           !controller_.Has_active_annotation_gesture();
}

bool OverlayWindow::Is_selection_stable_for_help() const {
    auto const &s = controller_.State();
    return !s.final_selection.Is_empty() && !s.dragging && !s.handle_dragging &&
           !s.move_dragging && !s.modifier_preview &&
           !controller_.Has_active_annotation_gesture();
}

std::wstring_view OverlayWindow::Hovered_toolbar_tooltip_text() const noexcept {
    if (color_wheel_.visible) {
        return {};
    }
    if (hotkey_help_overlay_.Is_visible()) {
        return {};
    }
    if (!controller_.Can_interact_with_annotation_toolbar()) {
        return {};
    }
    for (auto const &entry : toolbar_buttons_) {
        if (entry.button && entry.button->Is_hovered()) {
            return entry.tooltip;
        }
    }
    return {};
}

std::optional<core::RectPx> OverlayWindow::Hovered_toolbar_button_bounds() const {
    if (color_wheel_.visible) {
        return std::nullopt;
    }
    if (hotkey_help_overlay_.Is_visible()) {
        return std::nullopt;
    }
    if (!controller_.Can_interact_with_annotation_toolbar()) {
        return std::nullopt;
    }
    for (auto const &entry : toolbar_buttons_) {
        if (entry.button && entry.button->Is_hovered()) {
            return entry.button->Bounds();
        }
    }
    return std::nullopt;
}

LRESULT CALLBACK OverlayWindow::Static_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                                LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW const *create = reinterpret_cast<CREATESTRUCTW const *>(lparam);
        OverlayWindow *self = reinterpret_cast<OverlayWindow *>(create->lpCreateParams);
        if (!self) {
            return FALSE;
        }
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }

    OverlayWindow *self =
        reinterpret_cast<OverlayWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    LRESULT const result = self->Wnd_proc(msg, wparam, lparam);
    if (msg == WM_NCDESTROY) {
        self->hwnd_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return result;
}

void OverlayWindow::Apply_action(core::OverlayAction action) {
    if (action != core::OverlayAction::None) {
        Rebuild_toolbar_buttons();
    }
    if (color_wheel_.visible && !Can_show_color_wheel()) {
        Dismiss_color_wheel(false);
    }
    switch (action) {
    case core::OverlayAction::Repaint:
        InvalidateRect(hwnd_, nullptr, TRUE);
        break;
    case core::OverlayAction::InvalidateFrozenCache:
        if (d2d_resources_) {
            d2d_resources_->Invalidate_annotations();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    case core::OverlayAction::Close:
        Destroy();
        break;
    case core::OverlayAction::SaveDirect:
        Save_directly_and_close(false);
        break;
    case core::OverlayAction::SaveDirectAndCopyFile:
        Save_directly_and_close(true);
        break;
    case core::OverlayAction::SaveAs:
        Save_as_and_close(false);
        break;
    case core::OverlayAction::SaveAsAndCopyFile:
        Save_as_and_close(true);
        break;
    case core::OverlayAction::CopyToClipboard:
        Copy_to_clipboard_and_close();
        break;
    case core::OverlayAction::None:
        break;
    }
}

LRESULT OverlayWindow::On_key_down(WPARAM wparam, LPARAM lparam) {
    bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool const alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool const is_repeat = (lparam & (static_cast<LPARAM>(1) << 30)) != 0;
    bool const eff_ctrl = ctrl || (wparam == VK_CONTROL);
    bool const eff_shift = shift || (wparam == VK_SHIFT);
    bool const eff_alt = alt || (wparam == VK_MENU);
    bool const had_text_edit = controller_.Has_active_text_edit();

    if (wparam == VK_ESCAPE) {
        if (color_wheel_.visible) {
            Dismiss_color_wheel(true);
            return 0;
        }
        if (hotkey_help_overlay_.Is_visible()) {
            Hide_help_overlay(false);
            return 0;
        }
        core::OverlayAction const action = controller_.On_cancel();
        Apply_action(action);
        Cancel_highlighter_straighten_pending();
        if (!Is_open()) {
            return 0;
        }
        if (!controller_.Has_active_text_edit() && hwnd_ != nullptr) {
            caret_blink_visible_ = true;
            (void)KillTimer(hwnd_, kCaretBlinkTimerId);
        }
        (void)Refresh_hover_handle();
        Refresh_cursor();
        return 0;
    }
    if (color_wheel_.visible) {
        return 0;
    }
    if (had_text_edit) {
        core::TextEditController *const text_edit = controller_.Active_text_edit();
        if (text_edit == nullptr) {
            return 0;
        }

        auto repaint_text_draft = [&]() {
            Reset_caret_blink();
            Refresh_cursor();
            InvalidateRect(hwnd_, nullptr, FALSE);
        };

        if (wparam == VK_RETURN) {
            if (eff_ctrl) {
                text_edit->On_text_input(L"\n");
                repaint_text_draft();
            } else if (controller_.Commit_active_text_edit()) {
                if (d2d_resources_) {
                    d2d_resources_->Invalidate_annotations();
                }
                caret_blink_visible_ = true;
                (void)KillTimer(hwnd_, kCaretBlinkTimerId);
                (void)Refresh_hover_handle();
                Refresh_cursor();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        if (std::optional<core::TextNavigationAction> const action =
                Text_navigation_action_for_key(wparam, eff_ctrl);
            action.has_value()) {
            text_edit->On_navigation(*action, eff_shift);
            repaint_text_draft();
            return 0;
        }
        if (wparam == VK_BACK) {
            text_edit->On_backspace(eff_ctrl);
            repaint_text_draft();
            return 0;
        }
        if (wparam == VK_DELETE) {
            text_edit->On_delete(eff_ctrl);
            repaint_text_draft();
            return 0;
        }
        if (wparam == VK_INSERT) {
            text_edit->Toggle_insert_mode();
            repaint_text_draft();
            return 0;
        }
        if (eff_ctrl && wparam == L'B') {
            text_edit->Toggle_style(core::TextStyleToggle::Bold);
            repaint_text_draft();
            return 0;
        }
        if (eff_ctrl && wparam == L'I') {
            text_edit->Toggle_style(core::TextStyleToggle::Italic);
            repaint_text_draft();
            return 0;
        }
        if (eff_ctrl && wparam == L'U') {
            text_edit->Toggle_style(core::TextStyleToggle::Underline);
            repaint_text_draft();
            return 0;
        }
        if (eff_alt && eff_shift && wparam == L'5') {
            text_edit->Toggle_style(core::TextStyleToggle::Strikethrough);
            repaint_text_draft();
            return 0;
        }
        if (eff_ctrl && wparam == L'A') {
            text_edit->On_select_all();
            repaint_text_draft();
            return 0;
        }
        if (eff_ctrl && wparam == L'C') {
            std::wstring const copied_text = text_edit->Copy_selected_text();
            if (!copied_text.empty()) {
                Write_clipboard_text(copied_text);
            }
            return 0;
        }
        if (eff_ctrl && wparam == L'X') {
            std::wstring const cut_text = text_edit->Cut_selected_text();
            if (!cut_text.empty()) {
                Write_clipboard_text(cut_text);
                repaint_text_draft();
            }
            return 0;
        }
        if (eff_ctrl && wparam == L'V') {
            std::wstring clipboard_text;
            if (Read_clipboard_text(clipboard_text)) {
                text_edit->Paste_text(clipboard_text);
                repaint_text_draft();
            }
            return 0;
        }
        if (eff_ctrl && wparam == L'Z') {
            if (eff_shift) {
                text_edit->Redo();
            } else {
                text_edit->Undo();
            }
            repaint_text_draft();
            return 0;
        }
        return 0;
    }
    if (eff_ctrl && wparam == L'H') {
        if (!is_repeat) {
            if (hotkey_help_overlay_.Is_visible()) {
                Hide_help_overlay(false);
            } else {
                Show_help_overlay_at_cursor();
            }
        }
        return 0;
    }
    if (hotkey_help_overlay_.Is_visible()) {
        return 0;
    }
    if (eff_ctrl && wparam == L'Z') {
        if (eff_shift) {
            controller_.Redo();
        } else {
            controller_.Undo();
        }
        if (d2d_resources_) {
            d2d_resources_->Invalidate_annotations();
        }
        Rebuild_toolbar_buttons();
        (void)Refresh_hover_handle();
        Refresh_cursor();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    if (eff_ctrl && wparam == L'S') {
        Apply_action(controller_.On_save_requested(eff_shift, eff_alt));
        return 0;
    }
    if (eff_ctrl && wparam == L'C') {
        Apply_action(controller_.On_copy_to_clipboard_requested());
        return 0;
    }
    if (eff_ctrl && !eff_alt) {
        if (std::optional<int32_t> const delta = Brush_width_delta_for_key(wparam);
            delta.has_value()) {
            (void)Handle_tool_size_delta(*delta);
            return 0;
        }
    }
    if (wparam == VK_DELETE) {
        Apply_action(controller_.On_delete_selected_annotation());
        return 0;
    }
    if (!eff_ctrl && !eff_alt) {
        core::OverlayAction const action = controller_.On_annotation_tool_hotkey(
            static_cast<wchar_t>(wparam), eff_shift);
        if (action != core::OverlayAction::None) {
            Clear_transient_center_label(false);
            Apply_action(action);
            (void)Refresh_hover_handle();
            Refresh_cursor();
            return 0;
        }
    }
    if (wparam == VK_SHIFT || wparam == VK_CONTROL || wparam == VK_MENU) {
        core::OverlayModifierState new_mods{eff_shift, eff_ctrl, eff_alt};
        // Pre-resolve preview hints only when a preview update is needed.
        core::PointPx cursor_screen{};
        std::optional<core::RectPx> win_rect;
        core::RectPx vdesk{};
        std::optional<size_t> monitor_idx;
        int32_t ox = 0, oy = 0;
        auto const &s = controller_.State();
        bool const needs_preview = !s.dragging && !s.handle_dragging &&
                                   s.final_selection.Is_empty() &&
                                   (eff_shift || eff_ctrl);
        if (needs_preview) {
            cursor_screen = Get_cursor_pos_px();
            RECT wr{};
            GetWindowRect(hwnd_, &wr);
            ox = wr.left;
            oy = wr.top;
            win_rect = (eff_ctrl && !eff_shift)
                           ? window_query_->Get_window_rect_under_cursor(
                                 To_point(cursor_screen), hwnd_)
                           : std::nullopt;
            if (eff_shift && eff_ctrl) vdesk = Get_virtual_desktop_bounds_px();
            monitor_idx = (eff_shift && !eff_ctrl)
                              ? core::Index_of_monitor_containing(cursor_screen,
                                                                  s.cached_monitors)
                              : std::nullopt;
        }
        Apply_action(controller_.On_modifier_changed(new_mods, cursor_screen, win_rect,
                                                     vdesk, monitor_idx, ox, oy));
        return 0;
    }
    UINT const message_id =
        (lparam & (static_cast<LPARAM>(1) << 29)) != 0 ? WM_SYSKEYDOWN : WM_KEYDOWN;
    return DefWindowProcW(hwnd_, message_id, wparam, lparam);
}

LRESULT OverlayWindow::On_key_up(WPARAM wparam, LPARAM lparam) {
    if (color_wheel_.visible) {
        return 0;
    }
    if (hotkey_help_overlay_.Is_visible()) {
        return 0;
    }
    if (controller_.Has_active_text_edit()) {
        return 0;
    }

    // Force-clear the released key from the effective modifier state.
    bool const eff_shift =
        (wparam != VK_SHIFT) && (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool const eff_ctrl =
        (wparam != VK_CONTROL) && (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool const eff_alt = (wparam != VK_MENU) && (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (wparam == VK_SHIFT || wparam == VK_CONTROL || wparam == VK_MENU) {
        core::OverlayModifierState new_mods{eff_shift, eff_ctrl, eff_alt};
        // On key-up, no preview hints: preview is being cleared, not set.
        Apply_action(controller_.On_modifier_changed(new_mods, {}, {}, {}, {}, 0, 0));
        return 0;
    }
    UINT const message_id =
        (lparam & (static_cast<LPARAM>(1) << 29)) != 0 ? WM_SYSKEYUP : WM_KEYUP;
    return DefWindowProcW(hwnd_, message_id, wparam, lparam);
}

LRESULT OverlayWindow::On_char(WPARAM wparam) {
    core::TextEditController *const text_edit = controller_.Active_text_edit();
    if (text_edit == nullptr) {
        return 0;
    }

    // Ctrl+letter accelerators can still generate WM_CHAR (for example Ctrl+I as
    // tab). The shortcut is already handled in WM_KEYDOWN, so suppress the char.
    bool const ctrl_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool const alt_down = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (ctrl_down || alt_down) {
        return 0;
    }

    wchar_t const code_unit = static_cast<wchar_t>(wparam);
    if (code_unit < 32 && code_unit != L'\t') {
        return 0;
    }

    std::array<wchar_t, 1> text = {{code_unit}};
    text_edit->On_text_input(
        std::wstring_view(text.data(), static_cast<size_t>(text.size())));
    Reset_caret_blink();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return 0;
}

LRESULT OverlayWindow::On_l_button_down() {
    if (!resources_->capture.Is_valid()) {
        return 0;
    }
    bool const had_text_edit = controller_.Has_active_text_edit();
    core::PointPx const cur = Get_client_cursor_pos_px(hwnd_);
    if (color_wheel_.visible) {
        suppress_next_lbutton_up_ = true;
        auto const lbd_tool = controller_.Active_annotation_tool();
        bool const is_style_hub = lbd_tool == core::AnnotationToolId::Text ||
                                  lbd_tool == core::AnnotationToolId::Bubble;
        if (is_style_hub) {
            auto hub_hit = core::Hit_test_text_wheel_hub(color_wheel_.center, cur);
            if (hub_hit.has_value()) {
                color_wheel_.text_mode = (*hub_hit == core::TextWheelHubSide::Color)
                                             ? core::TextWheelMode::Color
                                             : core::TextWheelMode::Font;
                color_wheel_.hovered_hub = std::nullopt;
                color_wheel_.hovered_segment = std::nullopt;
                Refresh_cursor();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0; // wheel stays open
            }
        }
        std::optional<size_t> const segment = core::Hit_test_color_wheel_segment(
            color_wheel_.center, cur, Current_color_wheel_segment_count());
        Dismiss_color_wheel(false);
        if (segment.has_value()) {
            Select_color_wheel_segment(*segment);
        }
        Refresh_cursor();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    if (hotkey_help_overlay_.Is_visible()) {
        Hide_help_overlay(true);
        return 0;
    }
    if (!transient_center_label_text_.empty()) {
        Clear_transient_center_label(false);
    }
    // Route click to any toolbar button under the cursor (consume — do not start
    // dragging).
    if (!toolbar_buttons_.empty()) {
        ToolbarButtonEntry *hit_button = nullptr;
        for (auto &btn : toolbar_buttons_) {
            if (btn.button && btn.button->Hit_test(cur)) {
                hit_button = &btn;
                break;
            }
        }

        if (hit_button != nullptr) {
            if (controller_.Can_interact_with_annotation_toolbar()) {
                (void)Update_toolbar_hover_states(cur);
                hit_button->button->On_mouse_down(cur);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }

            if (had_text_edit) {
                if (core::TextEditController *const text_edit =
                        controller_.Active_text_edit();
                    text_edit != nullptr) {
                    core::TextDraftView const view = text_edit->Build_view();
                    bool const has_text = view.annotation != nullptr &&
                                          Text_annotation_has_text(*view.annotation);
                    if (has_text) {
                        (void)controller_.Commit_active_text_edit();
                        if (d2d_resources_) {
                            d2d_resources_->Invalidate_annotations();
                        }
                    } else {
                        controller_.Cancel_text_draft();
                    }
                    caret_blink_visible_ = true;
                    if (hwnd_ != nullptr) {
                        (void)KillTimer(hwnd_, kCaretBlinkTimerId);
                    }
                }
                (void)Update_toolbar_hover_states(cur);
                hit_button->button->On_mouse_down(cur);
                Refresh_cursor();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
        }
    }
    core::RectPx const before_selection = controller_.State().final_selection;
    bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool const alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    core::OverlayModifierState mods{shift, ctrl, alt};
    core::PointPx const cursor_client = cur;
    core::PointPx const cursor_screen = Get_cursor_pos_px();
    RECT wr{};
    GetWindowRect(hwnd_, &wr);

    // Pre-resolve only the hint relevant to the active preview mode.
    std::optional<HWND> win_handle;
    std::optional<size_t> monitor_idx;
    core::RectPx vdesk{};
    bool const in_preview = controller_.State().modifier_preview;
    if (ctrl && !shift && in_preview) {
        win_handle =
            window_query_->Get_window_under_cursor(To_point(cursor_screen), hwnd_);
    }
    if (shift && !ctrl && in_preview) {
        monitor_idx = core::Index_of_monitor_containing(
            cursor_screen, controller_.State().cached_monitors);
    }
    if (shift && ctrl && in_preview) {
        vdesk = Get_virtual_desktop_bounds_px();
    }

    Apply_action(
        controller_.On_primary_press(mods, cursor_client, cursor_screen, win_handle,
                                     monitor_idx, std::optional<core::RectPx>{}, vdesk,
                                     Collect_visible_snap_edges(), wr.left, wr.top));
    if (controller_.Has_active_annotation_gesture() ||
        controller_.Has_active_text_edit()) {
        Clear_transient_center_label(false);
    }
    if (controller_.Active_annotation_tool() == core::AnnotationToolId::Highlighter &&
        controller_.Has_active_annotation_gesture()) {
        int32_t const pause_ms =
            config_ != nullptr ? config_->highlighter_pause_straighten_ms
                               : core::AppConfig::kDefaultHighlighterPauseStraightenMs;
        if (pause_ms == 0) {
            if (controller_.Straighten_highlighter_stroke()) {
                Apply_action(core::OverlayAction::Repaint);
            }
            highlighter_straighten_pending_ = false;
        } else {
            highlighter_straighten_pending_ = true;
            highlighter_straighten_ref_pos_ = cursor_client;
            (void)SetTimer(hwnd_, kHighlighterStraightenTimerId,
                           static_cast<UINT>(pause_ms), nullptr);
        }
    }
    if (controller_.Has_active_text_edit()) {
        Reset_caret_blink();
    } else if (had_text_edit && hwnd_ != nullptr) {
        caret_blink_visible_ = true;
        (void)KillTimer(hwnd_, kCaretBlinkTimerId);
    }
    bool const hover_changed = Refresh_hover_handle();
    Refresh_cursor();
    if (hover_changed) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
    core::RectPx const after_selection = controller_.State().final_selection;
    auto const &after_state = controller_.State();
    if (before_selection != after_selection && before_selection.Is_empty() &&
        !after_selection.Is_empty() && !after_state.dragging &&
        !after_state.handle_dragging && !after_state.move_dragging) {
        controller_.Push_command(
            std::make_unique<core::ModificationCommand<core::RectPx>>(
                "Create selection",
                [this](core::RectPx const &r) {
                    controller_.Set_final_selection(r);
                    InvalidateRect(hwnd_, nullptr, TRUE);
                },
                before_selection, after_selection));
    }
    return 0;
}

LRESULT OverlayWindow::On_mouse_move() {
    if (color_wheel_.visible) {
        bool const hover_changed =
            Update_color_wheel_hover(Get_client_cursor_pos_px(hwnd_));
        bool const toolbar_changed = Clear_toolbar_hover_states();
        if (last_hover_handle_.has_value()) {
            last_hover_handle_ = std::nullopt;
        }
        if (hover_changed || toolbar_changed) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        Refresh_cursor();
        return 0;
    }
    if (hotkey_help_overlay_.Is_visible()) {
        return 0;
    }

    bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool const alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool const primary_down = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
    core::OverlayModifierState mods{shift, ctrl, alt, primary_down};
    core::PointPx const cursor_client = Get_client_cursor_pos_px(hwnd_);

    // Resolve expensive screen queries lazily — only when a preview update is needed.
    auto const &s = controller_.State();
    bool const needs_preview = !s.dragging && !s.handle_dragging && !s.move_dragging &&
                               s.final_selection.Is_empty() && (shift || ctrl);
    core::PointPx cursor_screen{};
    std::optional<core::RectPx> win_rect;
    core::RectPx vdesk{};
    std::optional<size_t> monitor_idx;
    int32_t ox = 0, oy = 0;
    if (needs_preview) {
        cursor_screen = Get_cursor_pos_px();
        RECT wr{};
        GetWindowRect(hwnd_, &wr);
        ox = wr.left;
        oy = wr.top;
        win_rect = (ctrl && !shift) ? window_query_->Get_window_rect_under_cursor(
                                          To_point(cursor_screen), hwnd_)
                                    : std::nullopt;
        if (shift && ctrl) vdesk = Get_virtual_desktop_bounds_px();
        monitor_idx =
            (shift && !ctrl)
                ? core::Index_of_monitor_containing(cursor_screen, s.cached_monitors)
                : std::nullopt;
    }
    core::OverlayAction const action = controller_.On_pointer_move(
        mods, cursor_client, cursor_screen, win_rect, vdesk, monitor_idx, ox, oy);
    Apply_action(action);
    if (highlighter_straighten_pending_) {
        int32_t const deadzone =
            config_ != nullptr ? config_->highlighter_pause_straighten_deadzone_px : 0;
        int32_t const dx = cursor_client.x - highlighter_straighten_ref_pos_.x;
        int32_t const dy = cursor_client.y - highlighter_straighten_ref_pos_.y;
        if (static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy >
            static_cast<int64_t>(deadzone) * deadzone) {
            int32_t const pause_ms =
                config_ != nullptr
                    ? config_->highlighter_pause_straighten_ms
                    : core::AppConfig::kDefaultHighlighterPauseStraightenMs;
            (void)KillTimer(hwnd_, kHighlighterStraightenTimerId);
            (void)SetTimer(hwnd_, kHighlighterStraightenTimerId,
                           static_cast<UINT>(pause_ms), nullptr);
            highlighter_straighten_ref_pos_ = cursor_client;
        }
    }
    if (controller_.Has_active_text_edit() && (GetKeyState(VK_LBUTTON) & 0x8000) != 0) {
        Reset_caret_blink();
    }

    if (Refresh_hover_handle()) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    if (Update_toolbar_hover_states(cursor_client)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (!controller_.Has_active_annotation_gesture() &&
        (Should_show_brush_cursor_preview() || Should_show_square_cursor_preview() ||
         controller_.Active_annotation_tool() == core::AnnotationToolId::Text)) {
        RedrawWindow(hwnd_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
    }

    return 0;
}

LRESULT OverlayWindow::On_mouse_wheel(WPARAM wparam) {
    if (color_wheel_.visible) {
        return 0;
    }
    if (hotkey_help_overlay_.Is_visible()) {
        return 0;
    }
    std::optional<core::AnnotationToolId> const active_tool =
        controller_.Active_annotation_tool();
    if (!active_tool.has_value()) {
        mouse_wheel_delta_remainder_ = 0;
        return 0;
    }

    mouse_wheel_delta_remainder_ += GET_WHEEL_DELTA_WPARAM(wparam);
    int32_t delta_steps = 0;
    while (mouse_wheel_delta_remainder_ >= WHEEL_DELTA) {
        ++delta_steps;
        mouse_wheel_delta_remainder_ -= WHEEL_DELTA;
    }
    while (mouse_wheel_delta_remainder_ <= -WHEEL_DELTA) {
        --delta_steps;
        mouse_wheel_delta_remainder_ += WHEEL_DELTA;
    }
    if (delta_steps != 0) {
        (void)Handle_tool_size_delta(delta_steps);
    }
    return 0;
}

LRESULT OverlayWindow::On_l_button_up() {
    if (suppress_next_lbutton_up_) {
        suppress_next_lbutton_up_ = false;
        return 0;
    }
    bool const had_text_edit = controller_.Has_active_text_edit();
    if (hotkey_help_overlay_.Is_visible()) {
        return 0;
    }
    // Route release to any toolbar button under the cursor.
    if (controller_.Can_interact_with_annotation_toolbar() &&
        !toolbar_buttons_.empty()) {
        core::PointPx const cur = Get_client_cursor_pos_px(hwnd_);
        bool const toolbar_hover_changed = Update_toolbar_hover_states(cur);
        for (auto const &btn : toolbar_buttons_) {
            if (btn.button && btn.button->Hit_test(cur)) {
                btn.button->On_mouse_up(cur);
                core::OverlayAction action = core::OverlayAction::None;
                if (btn.action == ToolbarButtonAction::SelectAnnotationTool &&
                    btn.tool_id.has_value()) {
                    action = controller_.On_select_annotation_tool(*btn.tool_id);
                    Clear_transient_center_label(false);
                    Apply_action(action);
                } else if (btn.action == ToolbarButtonAction::ShowHelp) {
                    Show_help_overlay_at_cursor();
                }
                bool const hover_changed = Refresh_hover_handle();
                if (action == core::OverlayAction::None) {
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
                if (hover_changed && action == core::OverlayAction::None) {
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
                Refresh_cursor();
                return 0;
            }
        }
        if (toolbar_hover_changed) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    auto const &s = controller_.State();
    core::RectPx const selection_before = s.final_selection;
    bool const was_drag = s.dragging;
    bool const was_move = s.move_dragging;
    bool const was_resize = s.handle_dragging;
    core::RectPx const before =
        was_move ? s.move_anchor_rect
                 : (was_resize ? s.resize_anchor_rect : core::RectPx{});

    Cancel_highlighter_straighten_pending();
    core::OverlayModifierState mods{};
    mods.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    Apply_action(controller_.On_primary_release(mods, Get_client_cursor_pos_px(hwnd_)));
    if (controller_.Has_active_text_edit()) {
        Reset_caret_blink();
    } else if (had_text_edit && hwnd_ != nullptr) {
        caret_blink_visible_ = true;
        (void)KillTimer(hwnd_, kCaretBlinkTimerId);
    }
    bool const hover_changed = Refresh_hover_handle();
    Refresh_cursor();
    if (hover_changed) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    core::RectPx const after = controller_.State().final_selection;
    if ((was_move || was_resize)) {
        if (before != after) {
            controller_.Push_command(
                std::make_unique<core::ModificationCommand<core::RectPx>>(
                    was_move ? "Move selection" : "Resize selection",
                    [this](core::RectPx const &r) {
                        controller_.Set_final_selection(r);
                        InvalidateRect(hwnd_, nullptr, TRUE);
                    },
                    before, after));
        }
    } else if (was_drag && selection_before != after && !after.Is_empty()) {
        controller_.Push_command(
            std::make_unique<core::ModificationCommand<core::RectPx>>(
                "Draw selection",
                [this](core::RectPx const &r) {
                    controller_.Set_final_selection(r);
                    InvalidateRect(hwnd_, nullptr, TRUE);
                },
                selection_before, after));
    }

    return 0;
}

LRESULT OverlayWindow::On_r_button_down() {
    if (!resources_->capture.Is_valid()) {
        return 0;
    }
    if (Can_show_color_wheel()) {
        Show_color_wheel(Get_client_cursor_pos_px(hwnd_));
    }
    return 0;
}

LRESULT OverlayWindow::On_timer(WPARAM wparam) {
    if (wparam == kHighlighterStraightenTimerId) {
        Cancel_highlighter_straighten_pending();
        if (controller_.Straighten_highlighter_stroke()) {
            Apply_action(core::OverlayAction::Repaint);
        }
        return 0;
    }
    if (wparam == kBrushSizeOverlayTimerId) {
        Clear_transient_center_label(true);
        return 0;
    }
    if (wparam == kCaretBlinkTimerId) {
        if (!controller_.Has_active_text_edit()) {
            caret_blink_visible_ = true;
            (void)KillTimer(hwnd_, kCaretBlinkTimerId);
            return 0;
        }
        caret_blink_visible_ = !caret_blink_visible_;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd_, WM_TIMER, wparam, 0);
}

bool OverlayWindow::Refresh_hover_handle() {
    auto const &s = controller_.State();
    if (!s.final_selection.Is_empty() && !s.dragging && !s.handle_dragging &&
        !s.move_dragging && !controller_.Has_active_annotation_gesture()) {
        core::PointPx const cur = Get_client_cursor_pos_px(hwnd_);
        std::optional<core::SelectionHandle> hover =
            core::Hit_test_border_zone(s.final_selection, cur);
        if (hover != last_hover_handle_) {
            last_hover_handle_ = hover;
            return true;
        }
        return false;
    }

    if (last_hover_handle_.has_value()) {
        last_hover_handle_ = std::nullopt;
        return true;
    }
    return false;
}

LRESULT OverlayWindow::Wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        return On_key_down(wparam, lparam);
    case WM_KEYUP:
    case WM_SYSKEYUP:
        return On_key_up(wparam, lparam);
    case WM_CHAR:
        return On_char(wparam);
    case WM_LBUTTONDOWN:
        return On_l_button_down();
    case WM_MOUSEMOVE:
        return On_mouse_move();
    case WM_MOUSEWHEEL:
        return On_mouse_wheel(wparam);
    case WM_LBUTTONUP:
        return On_l_button_up();
    case WM_RBUTTONDOWN:
        return On_r_button_down();
    case WM_CONTEXTMENU:
        return 0;
    case WM_PAINT:
        return On_paint();
    case WM_SETCURSOR:
        return On_set_cursor(wparam, lparam);
    case WM_TIMER:
        return On_timer(wparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        return On_destroy();
    case WM_CLOSE:
        return On_close();
    default:
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

std::wstring OverlayWindow::Resolve_default_save_directory() const {
    std::wstring dir;
    if (config_ && !config_->default_save_dir.empty()) {
        dir = config_->default_save_dir;
    } else {
        wchar_t pictures_dir[MAX_PATH] = {};
        SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, pictures_dir);
        dir = pictures_dir;
        dir += L"\\greenflame";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring OverlayWindow::Resolve_save_as_initial_directory() const {
    std::wstring dir;
    if (config_ && !config_->last_save_as_dir.empty()) {
        dir = config_->last_save_as_dir;
    } else if (config_ && !config_->default_save_dir.empty()) {
        dir = config_->default_save_dir;
    } else {
        wchar_t pictures_dir[MAX_PATH] = {};
        SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, pictures_dir);
        dir = pictures_dir;
        dir += L"\\greenflame";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void OverlayWindow::Build_default_save_name(std::wstring_view save_dir_for_num_scan,
                                            std::span<wchar_t> out) const {
    if (out.empty()) {
        return;
    }
    out[0] = L'\0';
    auto const &s = controller_.State();
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::wstring window_title;
    if (s.selection_window.has_value()) {
        wchar_t buffer[256] = {};
        if (GetWindowTextW(*s.selection_window, buffer, 256) > 0 && buffer[0]) {
            window_title = buffer;
        }
    }
    core::FilenamePatternContext ctx{};
    ctx.timestamp.day = st.wDay;
    ctx.timestamp.month = st.wMonth;
    ctx.timestamp.year = st.wYear;
    ctx.timestamp.hour = st.wHour;
    ctx.timestamp.minute = st.wMinute;
    ctx.timestamp.second = st.wSecond;
    ctx.monitor_index_zero_based = s.selection_monitor_index;
    ctx.window_title = window_title;

    std::wstring_view pattern;
    if (config_) {
        switch (s.selection_source) {
        case core::SaveSelectionSource::Region:
            pattern = config_->filename_pattern_region;
            break;
        case core::SaveSelectionSource::Desktop:
            pattern = config_->filename_pattern_desktop;
            break;
        case core::SaveSelectionSource::Monitor:
            pattern = config_->filename_pattern_monitor;
            break;
        case core::SaveSelectionSource::Window:
            pattern = config_->filename_pattern_window;
            break;
        }
    }

    std::wstring_view const effective_pattern =
        pattern.empty() ? core::Default_filename_pattern(s.selection_source) : pattern;
    if (core::Pattern_uses_num(effective_pattern)) {
        std::vector<std::wstring> const files =
            List_directory_filenames(save_dir_for_num_scan);
        ctx.incrementing_number =
            core::Find_next_num_for_pattern(effective_pattern, ctx, files);
    }

    std::wstring const name =
        core::Build_default_save_name(s.selection_source, ctx, pattern);
    size_t const n = name.copy(out.data(), out.size() - 1);
    out[n] = L'\0';
}

core::RectPx OverlayWindow::Selection_screen_rect() const {
    RECT overlay_rect{};
    GetWindowRect(hwnd_, &overlay_rect);
    core::RectPx const &sel = controller_.State().final_selection;
    return core::RectPx::From_ltrb(sel.left + static_cast<int32_t>(overlay_rect.left),
                                   sel.top + static_cast<int32_t>(overlay_rect.top),
                                   sel.right + static_cast<int32_t>(overlay_rect.left),
                                   sel.bottom + static_cast<int32_t>(overlay_rect.top));
}

bool OverlayWindow::Composite_annotations_into_capture(
    GdiCaptureResult &capture, core::RectPx target_bounds) const {
    std::span<const core::Annotation> const annotations = controller_.Annotations();
    if (!capture.Is_valid() || annotations.empty()) {
        return true;
    }

    HDC const dc = GetDC(nullptr);
    if (dc == nullptr) {
        return false;
    }

    bool ok = false;
    int const row_bytes = Row_bytes32(capture.width);
    size_t const buffer_size =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(capture.height);
    std::vector<uint8_t> pixels(buffer_size);
    BITMAPINFOHEADER bmi{};
    Fill_bmi32_top_down(bmi, capture.width, capture.height);
    if (GetDIBits(dc, capture.bitmap, 0, static_cast<UINT>(capture.height),
                  pixels.data(), reinterpret_cast<BITMAPINFO *>(&bmi),
                  DIB_RGB_COLORS) != 0) {
        core::Blend_annotations_onto_pixels(pixels, capture.width, capture.height,
                                            row_bytes, annotations, target_bounds);
        ok = SetDIBits(dc, capture.bitmap, 0, static_cast<UINT>(capture.height),
                       pixels.data(), reinterpret_cast<BITMAPINFO *>(&bmi),
                       DIB_RGB_COLORS) != 0;
    }
    ReleaseDC(nullptr, dc);
    return ok;
}

void OverlayWindow::Save_directly_and_close(bool copy_saved_file_to_clipboard) {
    if (!resources_->capture.Is_valid()) {
        return;
    }
    core::RectPx const &selection = controller_.State().final_selection;
    if (selection.Is_empty()) {
        return;
    }

    GdiCaptureResult cropped;
    if (!Crop_capture(resources_->capture, selection.left, selection.top,
                      selection.Width(), selection.Height(), cropped)) {
        Destroy();
        return;
    }
    if (!Composite_annotations_into_capture(cropped, selection)) {
        cropped.Free();
        Destroy();
        return;
    }

    std::wstring const save_dir = Resolve_default_save_directory();

    wchar_t default_name[256] = {};
    Build_default_save_name(save_dir, default_name);

    // Determine format from config.
    core::ImageSaveFormat format = core::ImageSaveFormat::Png;
    if (config_) {
        if (config_->default_save_format == L"jpg") {
            format = core::ImageSaveFormat::Jpeg;
        } else if (config_->default_save_format == L"bmp") {
            format = core::ImageSaveFormat::Bmp;
        }
    }

    std::wstring full_path = save_dir;
    if (!full_path.empty() && full_path.back() != L'\\') {
        full_path += L'\\';
    }
    full_path += default_name;
    full_path += core::Extension_for_image_save_format(format);
    std::wstring const reserved_path = Reserve_unique_file_path(full_path);
    if (reserved_path.empty()) {
        cropped.Free();
        Destroy();
        return;
    }

    bool const saved = Save_capture_to_file(cropped, reserved_path.c_str(), format);
    if (!saved) {
        (void)DeleteFileW(reserved_path.c_str());
        cropped.Free();
        Destroy();
        return;
    }
    bool file_copied_to_clipboard = false;
    if (copy_saved_file_to_clipboard) {
        file_copied_to_clipboard = Copy_file_path_to_clipboard(reserved_path, hwnd_);
    }
    if (config_) {
        config_->default_save_dir = save_dir;
        config_->Normalize();
    }
    Notify_save_and_close(cropped, reserved_path, file_copied_to_clipboard);
}

void OverlayWindow::Save_as_and_close(bool copy_saved_file_to_clipboard) {
    if (!resources_->capture.Is_valid()) {
        return;
    }
    core::RectPx const &selection = controller_.State().final_selection;
    if (selection.Is_empty()) {
        return;
    }

    GdiCaptureResult cropped;
    if (!Crop_capture(resources_->capture, selection.left, selection.top,
                      selection.Width(), selection.Height(), cropped)) {
        Destroy();
        return;
    }
    if (!Composite_annotations_into_capture(cropped, selection)) {
        cropped.Free();
        return;
    }

    std::wstring const initial_dir = Resolve_save_as_initial_directory();

    wchar_t default_name[256] = {};
    Build_default_save_name(initial_dir, default_name);

    OPENFILENAMEW ofn = {};
    std::array<wchar_t, MAX_PATH> path_buffer{};
    std::span<wchar_t> path_span(path_buffer);
    {
        std::wstring_view const sv(default_name);
        size_t const n = std::min(sv.size(), path_span.size() - 1);
        std::copy_n(sv.data(), n, path_span.begin());
        path_span[n] = L'\0';
    }
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"PNG (*.png)\0*.png\0JPEG "
                      L"(*.jpg;*.jpeg)\0*.jpg;*.jpeg\0BMP (*.bmp)\0*.bmp\0\0";
    ofn.lpstrFile = path_buffer.data();
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!initial_dir.empty()) {
        ofn.lpstrInitialDir = initial_dir.c_str();
    }

    if (!GetSaveFileNameW(&ofn)) {
        cropped.Free();
        return;
    }

    std::wstring const resolved_path = core::Ensure_image_save_extension(
        std::wstring_view(path_buffer.data()), ofn.nFilterIndex);
    size_t const n = resolved_path.copy(path_buffer.data(), path_span.size() - 1);
    path_span[n] = L'\0';

    core::ImageSaveFormat const format =
        core::Detect_image_save_format_from_path(std::wstring_view(path_buffer.data()));
    bool const saved = Save_capture_to_file(cropped, path_buffer.data(), format);

    if (!saved) {
        cropped.Free();
        return;
    }
    bool file_copied_to_clipboard = false;
    if (copy_saved_file_to_clipboard) {
        file_copied_to_clipboard =
            Copy_file_path_to_clipboard(std::wstring_view(path_buffer.data()), hwnd_);
    }
    std::wstring_view const path_view(path_buffer.data());
    size_t const last_slash_pos = path_view.rfind(L'\\');
    if (last_slash_pos != std::wstring_view::npos && config_) {
        size_t const dir_len = last_slash_pos;
        if (dir_len < path_span.size()) {
            config_->last_save_as_dir.assign(path_buffer.data(), dir_len);
            config_->Normalize();
        }
    }
    Notify_save_and_close(cropped, path_view, file_copied_to_clipboard);
}

void OverlayWindow::Notify_save_and_close(GdiCaptureResult &cropped,
                                          std::wstring_view saved_path,
                                          bool file_copied_to_clipboard) {
    HBITMAP thumb = Create_thumbnail_from_capture(cropped);
    cropped.Free();
    if (events_) {
        events_->On_selection_saved_to_file(Selection_screen_rect(),
                                            controller_.State().selection_window, thumb,
                                            saved_path, file_copied_to_clipboard);
        thumb = nullptr;
    }
    if (thumb != nullptr) {
        DeleteObject(thumb);
    }
    Destroy();
}

void OverlayWindow::Copy_to_clipboard_and_close() {
    if (!resources_->capture.Is_valid()) {
        return;
    }
    core::RectPx const &selection = controller_.State().final_selection;
    if (selection.Is_empty()) {
        return;
    }

    GdiCaptureResult cropped;
    if (!Crop_capture(resources_->capture, selection.left, selection.top,
                      selection.Width(), selection.Height(), cropped)) {
        Destroy();
        return;
    }
    if (!Composite_annotations_into_capture(cropped, selection)) {
        cropped.Free();
        Destroy();
        return;
    }
    bool const copied_to_clipboard = Copy_capture_to_clipboard(cropped, hwnd_);
    cropped.Free();

    if (copied_to_clipboard && events_) {
        events_->On_selection_copied_to_clipboard(Selection_screen_rect(),
                                                  controller_.State().selection_window);
    }
    Destroy();
}

// ---------------------------------------------------------------------------
// Shared paint-input state gathering for the Direct2D overlay renderer.
// ---------------------------------------------------------------------------

namespace {

void Gather_monitor_client_rects(HWND hwnd,
                                 std::vector<core::MonitorWithBounds> const &monitors,
                                 std::vector<core::RectPx> &out) {
    out.clear();
    out.reserve(monitors.size());
    RECT overlay_rect{};
    if (GetWindowRect(hwnd, &overlay_rect) == 0) {
        return;
    }
    for (auto const &monitor : monitors) {
        out.push_back(core::RectPx::From_ltrb(
            monitor.bounds.left - static_cast<int32_t>(overlay_rect.left),
            monitor.bounds.top - static_cast<int32_t>(overlay_rect.top),
            monitor.bounds.right - static_cast<int32_t>(overlay_rect.left),
            monitor.bounds.bottom - static_cast<int32_t>(overlay_rect.top)));
    }
}

} // namespace

LRESULT OverlayWindow::On_paint() {
    PAINTSTRUCT paint{};
    HDC const hdc = BeginPaint(hwnd_, &paint);
    if (!hdc) {
        return 0;
    }

    RECT rect{};
    GetClientRect(hwnd_, &rect);
    auto const &s = controller_.State();

    hotkey_help_overlay_.Hide_if_selection_unstable(Is_selection_stable_for_help());

    std::vector<core::RectPx> monitor_client_rects;
    Gather_monitor_client_rects(hwnd_, s.cached_monitors, monitor_client_rects);

    core::PointPx cursor = Get_client_cursor_pos_px(hwnd_);
    bool const snap_enabled = (GetKeyState(VK_MENU) & 0x8000) == 0;
    bool const crosshair_mode = s.final_selection.Is_empty() && !s.dragging &&
                                !s.handle_dragging && !s.modifier_preview;
    if (crosshair_mode && snap_enabled) {
        cursor = core::Snap_point_to_fullscreen_crosshair_edges(
            cursor, s.vertical_edges, s.horizontal_edges, kSnapThresholdPx);
        if (cursor.x >= rect.right) cursor.x = rect.right - 1;
        if (cursor.y >= rect.bottom) cursor.y = rect.bottom - 1;
    }

    std::vector<IOverlayButton *> btn_ptrs;
    btn_ptrs.reserve(toolbar_buttons_.size());
    for (auto const &u : toolbar_buttons_) {
        btn_ptrs.push_back(u.button.get());
    }

    // Build per-button D2D glyph pointers (parallel to btn_ptrs).
    std::vector<ID2D1Bitmap *> btn_glyphs;
    if (d2d_resources_) {
        btn_glyphs.reserve(toolbar_buttons_.size());
        for (auto const &u : toolbar_buttons_) {
            btn_glyphs.push_back(d2d_resources_->Toolbar_glyph_bitmap(u.glyph));
        }
    }

    // --- D2D render path ---
    if (d2d_resources_) {
        if (!d2d_resources_->annotations_valid) {
            Rebuild_annotations_bitmap(*d2d_resources_, controller_.Annotations());
        }
        if (!d2d_resources_->frozen_valid) {
            Rebuild_frozen_bitmap(*d2d_resources_, s.final_selection,
                                  resources_->capture.width,
                                  resources_->capture.height);
        }

        D2DPaintInput input{};
        std::optional<core::TextDraftView> draft_text_view = std::nullopt;
        if (core::TextEditController *const text_edit = controller_.Active_text_edit();
            text_edit != nullptr) {
            draft_text_view = text_edit->Build_view();
        }
        input.dragging = s.dragging;
        input.handle_dragging = s.handle_dragging;
        input.move_dragging = s.move_dragging;
        input.annotation_editing = controller_.Has_active_annotation_edit();
        input.modifier_preview = s.modifier_preview;
        if (config_ != nullptr) {
            input.show_selection_size_side_labels =
                config_->show_selection_size_side_labels;
            input.show_selection_size_center_label =
                config_->show_selection_size_center_label;
        }
        input.live_rect = s.live_rect;
        input.final_selection = s.final_selection;
        input.monitor_rects_client =
            std::span<const core::RectPx>(monitor_client_rects);
        input.cursor_client_px = cursor;
        input.annotations = controller_.Annotations();
        input.draft_freehand_points = controller_.Draft_freehand_points();
        input.draft_freehand_style = controller_.Draft_freehand_style();
        input.draft_freehand_tip_shape =
            controller_.Active_annotation_tool() == core::AnnotationToolId::Highlighter
                ? core::FreehandTipShape::Square
                : core::FreehandTipShape::Round;
        input.draft_freehand_blit_opacity =
            controller_.Active_annotation_tool() == core::AnnotationToolId::Highlighter
                ? static_cast<float>(std::clamp(
                      config_ != nullptr ? config_->highlighter_opacity_percent
                                         : core::kDefaultHighlighterOpacityPercent,
                      core::StrokeStyle::kMinOpacityPercent,
                      core::StrokeStyle::kMaxOpacityPercent)) /
                      100.f
                : 1.0f;
        if (draft_text_view.has_value()) {
            input.draft_text_annotation = draft_text_view->annotation;
            input.draft_text_selection_rects =
                std::move(draft_text_view->selection_rects);
            input.draft_text_caret_rect = draft_text_view->insert_mode
                                              ? draft_text_view->caret_rect
                                              : draft_text_view->overwrite_caret_rect;
            input.draft_text_insert_mode = draft_text_view->insert_mode;
            input.draft_text_blink_visible = caret_blink_visible_;
        } else if (!input.draft_freehand_style.has_value() ||
                   input.draft_freehand_points.size() == 1) {
            input.draft_annotation = controller_.Draft_annotation();
        }
        if (controller_.Should_show_selected_annotation_handles()) {
            input.selected_annotation = controller_.Selected_annotation();
            input.selected_annotation_bounds = controller_.Selected_annotation_bounds();
        }
        input.transient_center_label_text = transient_center_label_text_;
        input.toolbar_tooltip_text = Hovered_toolbar_tooltip_text();
        input.hovered_toolbar_bounds = Hovered_toolbar_button_bounds();
        input.show_color_wheel = color_wheel_.visible;
        input.color_wheel_center_px = color_wheel_.center;
        input.color_wheel_colors = Current_tool_color_palette();
        auto const active_tool_for_wheel = controller_.Active_annotation_tool();
        bool const is_style_hub =
            active_tool_for_wheel == core::AnnotationToolId::Text ||
            active_tool_for_wheel == core::AnnotationToolId::Bubble;
        input.color_wheel_has_style_hub = is_style_hub;
        if (is_style_hub) {
            input.text_wheel_active_mode = color_wheel_.text_mode;
            input.text_wheel_hovered_hub = color_wheel_.hovered_hub;
            std::array<std::wstring_view, 4> const font_families =
                Resolve_text_font_families(config_);
            core::TextFontChoice const hub_font =
                (active_tool_for_wheel == core::AnnotationToolId::Bubble)
                    ? controller_.Bubble_current_font()
                    : controller_.Text_current_font();
            input.text_wheel_hub_font_family =
                font_families[core::Text_font_choice_index(hub_font)];
            input.color_wheel_font_families = font_families;
            if (color_wheel_.text_mode == core::TextWheelMode::Color) {
                input.color_wheel_segment_count = core::kAnnotationColorSlotCount;
                input.color_wheel_selected_segment = Current_annotation_color_index();
            } else {
                input.color_wheel_segment_count = kTextWheelFontChoices.size();
                input.color_wheel_selected_segment =
                    core::Text_font_choice_index(hub_font);
            }
            input.color_wheel_hovered_segment = color_wheel_.hovered_segment;
        } else {
            input.color_wheel_segment_count = Current_color_wheel_segment_count();
            input.color_wheel_selected_segment = Current_annotation_color_index();
            input.color_wheel_hovered_segment = color_wheel_.hovered_segment;
        }
        if (s.handle_dragging && s.resize_handle.has_value()) {
            input.highlight_handle = s.resize_handle;
        } else if (!s.move_dragging && !s.dragging && !s.modifier_preview &&
                   !controller_.Has_active_annotation_gesture() &&
                   !color_wheel_.visible) {
            input.highlight_handle = last_hover_handle_;
        }
        if (Should_show_brush_cursor_preview()) {
            input.brush_cursor_preview_width_px =
                controller_.Tool_physical_size(*controller_.Active_annotation_tool());
        }
        if (Should_show_square_cursor_preview()) {
            if (controller_.Active_annotation_tool() == core::AnnotationToolId::Arrow) {
                input.arrow_cursor_preview_width_px = controller_.Tool_physical_size(
                    *controller_.Active_annotation_tool());
            } else {
                input.square_cursor_preview_width_px = controller_.Tool_physical_size(
                    *controller_.Active_annotation_tool());
            }
        }
        if (!color_wheel_.visible && !s.final_selection.Is_empty() && !s.dragging &&
            !s.handle_dragging && !s.move_dragging && !s.modifier_preview &&
            !last_hover_handle_.has_value() &&
            !controller_.Has_active_annotation_gesture() &&
            controller_.Active_annotation_tool() == core::AnnotationToolId::Text) {
            input.text_cursor_preview_style = core::TextAnnotationBaseStyle{
                controller_.Annotation_color(),
                controller_.Text_current_font(),
                controller_.Text_point_size(),
            };
            input.color_wheel_font_families = Resolve_text_font_families(config_);
        }
        input.toolbar_buttons = std::span<IOverlayButton *const>(btn_ptrs);
        input.toolbar_button_glyphs = std::span<ID2D1Bitmap *const>(btn_glyphs);

        bool const ok =
            Paint_d2d_frame(*d2d_resources_, input, resources_->capture.width,
                            resources_->capture.height, &hotkey_help_overlay_);
        if (!ok) {
            Handle_device_loss();
        }

        EndPaint(hwnd_, &paint);
        return 0;
    }

    // D2D resources unavailable; ensure paint cycle completes.
    EndPaint(hwnd_, &paint);
    return 0;
}

void OverlayWindow::Handle_device_loss() {
    if (!d2d_resources_ || !resources_->capture.Is_valid()) {
        return;
    }
    int const w = resources_->capture.width;
    int const h = resources_->capture.height;

    d2d_resources_->Release_device_resources();
    if (!d2d_resources_->Create_hwnd_rt(hwnd_, w, h) ||
        !d2d_resources_->Upload_screenshot(resources_->capture) ||
        !d2d_resources_->Create_shared_resources() ||
        !d2d_resources_->Create_cache_targets(w, h)) {
        controller_.Set_text_layout_engine(nullptr);
        text_layout_engine_.reset();
        d2d_resources_.reset();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return;
    }
    auto const glyphs = resources_->Glyph_pointers();
    (void)d2d_resources_->Upload_glyph_bitmaps(
        std::span<OverlayButtonGlyph const *const>(glyphs));
    if (!text_layout_engine_ && d2d_resources_->factory &&
        d2d_resources_->dwrite_factory) {
        text_layout_engine_ = std::make_unique<D2DTextLayoutEngine>(
            d2d_resources_->factory.Get(), d2d_resources_->dwrite_factory.Get());
    }
    if (text_layout_engine_) {
        text_layout_engine_->Set_font_families(Resolve_text_font_families(config_));
    }
    controller_.Set_text_layout_engine(text_layout_engine_.get());
    d2d_resources_->Invalidate_annotations();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT OverlayWindow::On_destroy() {
    Clear_transient_center_label(false);
    caret_blink_visible_ = true;
    (void)KillTimer(hwnd_, kCaretBlinkTimerId);
    Cancel_highlighter_straighten_pending();
    mouse_wheel_delta_remainder_ = 0;
    suppress_next_lbutton_up_ = false;
    color_wheel_ = {};
    toolbar_buttons_.clear();
    controller_.Set_text_layout_engine(nullptr);
    text_layout_engine_.reset();
    if (d2d_resources_) {
        d2d_resources_->Release_all();
        d2d_resources_.reset();
    }
    resources_->Reset();
    controller_.Reset_for_session({});
    if (events_) {
        events_->On_overlay_closed();
    }
    return 0;
}

LRESULT OverlayWindow::On_close() {
    Destroy();
    return 0;
}

void OverlayWindow::Refresh_cursor() {
    if (color_wheel_.visible) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }
    if (hotkey_help_overlay_.Is_visible()) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }
    if (!toolbar_buttons_.empty()) {
        core::PointPx const cursor = Get_client_cursor_pos_px(hwnd_);
        for (auto const &entry : toolbar_buttons_) {
            if (entry.button != nullptr && entry.button->Hit_test(cursor)) {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                return;
            }
        }
    }

    auto const &s = controller_.State();
    if (s.move_dragging || controller_.Is_annotation_dragging()) {
        SetCursor(Move_mode_cursor());
        return;
    }
    if (std::optional<core::AnnotationEditHandleKind> const active_handle =
            controller_.Active_annotation_edit_handle();
        active_handle.has_value()) {
        if (std::optional<core::SelectionHandle> const selection_handle =
                Selection_handle_for_active_annotation_edit(*active_handle);
            selection_handle.has_value()) {
            SetCursor(Cursor_for_handle(*selection_handle));
        } else {
            SetCursor(Load_annotation_tool_cursor(hinstance_));
        }
        return;
    }
    if (controller_.Has_active_text_edit()) {
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        return;
    }
    if (controller_.Has_active_annotation_gesture()) {
        SetCursor(Load_annotation_tool_cursor(hinstance_));
        return;
    }
    if (s.handle_dragging && s.resize_handle.has_value()) {
        SetCursor(Cursor_for_handle(*s.resize_handle));
        return;
    }
    if (s.modifier_preview) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }
    if (!s.final_selection.Is_empty() && !s.dragging) {
        core::PointPx const cursor = Get_client_cursor_pos_px(hwnd_);
        std::optional<core::SelectionHandle> hit =
            core::Hit_test_border_zone(s.final_selection, cursor);
        if (hit.has_value()) {
            SetCursor(Cursor_for_handle(*hit));
            return;
        }
        if (std::optional<core::AnnotationEditTarget> const edit_target =
                controller_.Annotation_edit_target_at(cursor);
            edit_target.has_value()) {
            if (std::optional<core::SelectionHandle> const selection_handle =
                    Selection_handle_for_annotation_edit_target(edit_target->kind);
                selection_handle.has_value()) {
                SetCursor(Cursor_for_handle(*selection_handle));
                return;
            }
            if (edit_target->kind == core::AnnotationEditTargetKind::LineStartHandle ||
                edit_target->kind == core::AnnotationEditTargetKind::LineEndHandle) {
                SetCursor(Load_annotation_tool_cursor(hinstance_));
                return;
            }
        }
        std::optional<core::AnnotationToolId> const active_tool =
            controller_.Active_annotation_tool();
        if (!active_tool.has_value()) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return;
        }
        if (*active_tool == core::AnnotationToolId::Freehand ||
            *active_tool == core::AnnotationToolId::Highlighter ||
            *active_tool == core::AnnotationToolId::Line ||
            *active_tool == core::AnnotationToolId::Arrow ||
            *active_tool == core::AnnotationToolId::Rectangle ||
            *active_tool == core::AnnotationToolId::FilledRectangle ||
            *active_tool == core::AnnotationToolId::Ellipse ||
            *active_tool == core::AnnotationToolId::FilledEllipse) {
            SetCursor(Load_annotation_tool_cursor(hinstance_));
        } else if (*active_tool == core::AnnotationToolId::Text) {
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        } else {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
        return;
    }
    if (controller_.Active_annotation_tool() == core::AnnotationToolId::Text) {
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        return;
    }
    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
}

LRESULT OverlayWindow::On_set_cursor(WPARAM wparam, LPARAM lparam) {
    if (LOWORD(lparam) != HTCLIENT) {
        return DefWindowProcW(hwnd_, WM_SETCURSOR, wparam, lparam);
    }
    Refresh_cursor();
    return TRUE;
}

} // namespace greenflame
