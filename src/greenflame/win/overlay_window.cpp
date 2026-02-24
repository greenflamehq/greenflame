// Overlay window object: fullscreen capture/selection UI.

#include "win/overlay_window.h"

#include "app_config.h"
#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/save_image_policy.h"
#include "greenflame_core/selection_handles.h"
#include "greenflame_core/snap_edge_builder.h"
#include "greenflame_core/snap_to_edges.h"
#include "win/display_queries.h"
#include "win/gdi_capture.h"
#include "win/overlay_paint.h"
#include "win/save_image.h"
#include "win/ui_palette.h"
#include "win/window_query.h"

namespace {

constexpr wchar_t kOverlayWindowClass[] = L"GreenflameOverlay";
constexpr int kHandleGrabRadiusPx = 6;
constexpr int32_t kSnapThresholdPx = 10;
constexpr int kDimensionFontHeight = 14;
constexpr int kCenterFontHeight = 36;

[[nodiscard]] bool Is_snap_enabled() noexcept {
    return (GetKeyState(VK_MENU) & 0x8000) == 0;
}

constexpr int kThumbnailMaxWidth = 320;
constexpr int kThumbnailMaxHeight = 120;

[[nodiscard]] HBITMAP
Create_thumbnail_from_capture(greenflame::GdiCaptureResult const &capture) {
    if (!capture.Is_valid()) {
        return nullptr;
    }
    float const scale_w = static_cast<float>(kThumbnailMaxWidth) / capture.width;
    float const scale_h = static_cast<float>(kThumbnailMaxHeight) / capture.height;
    float scale = (std::min)(scale_w, scale_h);
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    int tw = static_cast<int>(capture.width * scale);
    int th = static_cast<int>(capture.height * scale);
    if (tw <= 0) {
        tw = 1;
    }
    if (th <= 0) {
        th = 1;
    }
    HBITMAP result = nullptr;
    HDC const screen_dc = GetDC(nullptr);
    if (screen_dc != nullptr) {
        HDC const src_dc = CreateCompatibleDC(screen_dc);
        HDC const dst_dc = CreateCompatibleDC(screen_dc);
        result = CreateCompatibleBitmap(screen_dc, tw, th);
        if (src_dc != nullptr && dst_dc != nullptr && result != nullptr) {
            HGDIOBJ const old_src = SelectObject(src_dc, capture.bitmap);
            HGDIOBJ const old_dst = SelectObject(dst_dc, result);
            SetStretchBltMode(dst_dc, HALFTONE);
            SetBrushOrgEx(dst_dc, 0, 0, nullptr);
            StretchBlt(dst_dc, 0, 0, tw, th, src_dc, 0, 0, capture.width,
                       capture.height, SRCCOPY);
            SelectObject(dst_dc, old_dst);
            SelectObject(src_dc, old_src);
        } else if (result != nullptr) {
            DeleteObject(result);
            result = nullptr;
        }
        if (dst_dc != nullptr) {
            DeleteDC(dst_dc);
        }
        if (src_dc != nullptr) {
            DeleteDC(src_dc);
        }
        ReleaseDC(nullptr, screen_dc);
    }
    return result;
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

} // namespace

namespace greenflame {

struct OverlayWindow::OverlayResources {
    GdiCaptureResult capture = {};
    std::vector<uint8_t> paint_buffer = {};
    PaintResources paint = {};

    OverlayResources() = default;
    ~OverlayResources() { Reset(); }

    OverlayResources(OverlayResources const &) = delete;
    OverlayResources &operator=(OverlayResources const &) = delete;

    [[nodiscard]] bool Initialize_for_capture() {
        if (!capture.Is_valid()) {
            return false;
        }
        int const paint_row_bytes = Row_bytes32(capture.width);
        size_t const paint_buf_size =
            static_cast<size_t>(paint_row_bytes) * static_cast<size_t>(capture.height);
        try {
            paint_buffer.resize(paint_buf_size);
        } catch (...) {
            return false;
        }

        paint.font_dim =
            CreateFontW(kDimensionFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, FF_DONTCARE, L"Segoe UI");
        paint.font_center =
            CreateFontW(kCenterFontHeight, 0, 0, 0, FW_BLACK, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, FF_DONTCARE, L"Segoe UI");
        paint.crosshair_pen = CreatePen(PS_SOLID, 1, winui::kOverlayCrosshair);
        paint.border_pen = CreatePen(PS_SOLID, 1, winui::kOverlayBorder);
        paint.handle_brush = CreateSolidBrush(winui::kOverlayHandle);
        paint.handle_pen = CreatePen(PS_SOLID, 1, winui::kOverlayHandle);
        paint.sel_border_brush = CreateSolidBrush(winui::kOverlayHandle);
        return true;
    }

    void Reset() noexcept {
        capture.Free();
        paint_buffer.clear();
        if (paint.font_dim) {
            DeleteObject(paint.font_dim);
            paint.font_dim = nullptr;
        }
        if (paint.font_center) {
            DeleteObject(paint.font_center);
            paint.font_center = nullptr;
        }
        if (paint.crosshair_pen) {
            DeleteObject(paint.crosshair_pen);
            paint.crosshair_pen = nullptr;
        }
        if (paint.border_pen) {
            DeleteObject(paint.border_pen);
            paint.border_pen = nullptr;
        }
        if (paint.handle_brush) {
            DeleteObject(paint.handle_brush);
            paint.handle_brush = nullptr;
        }
        if (paint.handle_pen) {
            DeleteObject(paint.handle_pen);
            paint.handle_pen = nullptr;
        }
        if (paint.sel_border_brush) {
            DeleteObject(paint.sel_border_brush);
            paint.sel_border_brush = nullptr;
        }
    }
};

struct OverlayWindow::OverlayState {
    bool dragging = false;
    bool handle_dragging = false;
    bool move_dragging = false;
    bool modifier_preview = false;
    std::optional<greenflame::core::SelectionHandle> resize_handle = std::nullopt;
    greenflame::core::RectPx resize_anchor_rect = {};
    greenflame::core::PointPx move_grab_offset = {};
    greenflame::core::RectPx move_anchor_rect = {};
    greenflame::core::PointPx start_px = {};
    greenflame::core::RectPx live_rect = {};
    greenflame::core::RectPx final_selection = {};
    greenflame::core::SaveSelectionSource selection_source =
        greenflame::core::SaveSelectionSource::Region;
    std::optional<HWND> selection_window = std::nullopt;
    std::optional<size_t> selection_monitor_index = std::nullopt;
    ULONGLONG last_invalidate_tick = 0;
    std::vector<greenflame::core::RectPx> window_rects = {};
    std::vector<int32_t> vertical_edges = {};
    std::vector<int32_t> horizontal_edges = {};
    std::vector<greenflame::core::MonitorWithBounds> cached_monitors = {};

    void Reset_for_session() {
        dragging = false;
        handle_dragging = false;
        move_dragging = false;
        modifier_preview = false;
        resize_handle = std::nullopt;
        resize_anchor_rect = {};
        move_grab_offset = {};
        move_anchor_rect = {};
        start_px = {};
        live_rect = {};
        final_selection = {};
        selection_source = greenflame::core::SaveSelectionSource::Region;
        selection_window = std::nullopt;
        selection_monitor_index = std::nullopt;
        last_invalidate_tick = 0;
        window_rects.clear();
        vertical_edges.clear();
        horizontal_edges.clear();
        cached_monitors.clear();
    }
};

OverlayWindow::OverlayWindow(IOverlayEvents *events, AppConfig *config)
    : events_(events), config_(config), state_(std::make_unique<OverlayState>()),
      resources_(std::make_unique<OverlayResources>()) {}

OverlayWindow::~OverlayWindow() { Destroy(); }

bool OverlayWindow::Register_window_class(HINSTANCE hinstance) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
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
    hinstance_ = hinstance;
    resources_->Reset();
    state_->Reset_for_session();
    state_->window_rects.reserve(64);
    state_->vertical_edges.reserve(128);
    state_->horizontal_edges.reserve(128);

    core::RectPx const bounds = Get_virtual_desktop_bounds_px();
    HWND const hwnd = CreateWindowExW(
        WS_EX_TOPMOST, kOverlayWindowClass, L"", WS_POPUP, bounds.left, bounds.top,
        bounds.Width(), bounds.Height(), nullptr, nullptr, hinstance_, this);
    if (!hwnd) {
        hinstance_ = nullptr;
        return false;
    }

    if (!Capture_virtual_desktop(resources_->capture) ||
        !resources_->Initialize_for_capture()) {
        DestroyWindow(hwnd);
        return false;
    }

    state_->cached_monitors = Get_monitors_with_bounds();
    Build_snap_edges_from_windows();
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

LRESULT OverlayWindow::Wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_KEYDOWN:
        return On_key_down(wparam, lparam);
    case WM_KEYUP:
        return On_key_up(wparam, lparam);
    case WM_LBUTTONDOWN:
        return On_l_button_down();
    case WM_MOUSEMOVE:
        return On_mouse_move();
    case WM_LBUTTONUP:
        return On_l_button_up();
    case WM_PAINT:
        return On_paint();
    case WM_SETCURSOR:
        return On_set_cursor(wparam, lparam);
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

std::vector<std::wstring>
OverlayWindow::List_directory_filenames(std::wstring_view dir) {
    std::vector<std::wstring> result;
    std::wstring search_path(dir);
    if (!search_path.empty() && search_path.back() != L'\\') {
        search_path += L'\\';
    }
    search_path += L'*';
    WIN32_FIND_DATAW fd{};
    HANDLE const h = FindFirstFileW(search_path.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return result;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            result.emplace_back(fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return result;
}

void OverlayWindow::Build_default_save_name(std::wstring_view save_dir_for_num_scan,
                                            wchar_t *out, size_t out_chars) const {
    if (!out || out_chars == 0) {
        return;
    }
    out[0] = L'\0';
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::wstring window_title;
    if (state_->selection_window.has_value()) {
        wchar_t buffer[256] = {};
        if (GetWindowTextW(*state_->selection_window, buffer, 256) > 0 && buffer[0]) {
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
    ctx.monitor_index_zero_based = state_->selection_monitor_index;
    ctx.window_title = window_title;

    std::wstring_view pattern;
    if (config_) {
        switch (state_->selection_source) {
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

    // Resolve ${num} by directory-scanning if the pattern uses it.
    std::wstring_view const effective_pattern =
        pattern.empty() ? core::Default_filename_pattern(state_->selection_source)
                        : pattern;
    if (core::Pattern_uses_num(effective_pattern)) {
        std::vector<std::wstring> const files =
            List_directory_filenames(save_dir_for_num_scan);
        ctx.incrementing_number =
            core::Find_next_num_for_pattern(effective_pattern, ctx, files);
    }

    std::wstring const name =
        core::Build_default_save_name(state_->selection_source, ctx, pattern);
    wcsncpy_s(out, out_chars, name.c_str(), _TRUNCATE);
}

void OverlayWindow::Build_snap_edges_from_windows() {
    RECT overlay_rect{};
    GetWindowRect(hwnd_, &overlay_rect);
    int const origin_x = overlay_rect.left;
    int const origin_y = overlay_rect.top;
    state_->window_rects.clear();
    Get_visible_top_level_window_rects(hwnd_, state_->window_rects);
    for (core::MonitorWithBounds const &monitor : state_->cached_monitors) {
        state_->window_rects.push_back(monitor.bounds);
    }
    core::SnapEdges const edges = core::Build_snap_edges_from_screen_rects(
        state_->window_rects, origin_x, origin_y);
    state_->vertical_edges = edges.vertical;
    state_->horizontal_edges = edges.horizontal;
}

void OverlayWindow::Update_modifier_preview(bool shift, bool ctrl) {
    if (state_->dragging || state_->handle_dragging) {
        return;
    }
    if (!state_->final_selection.Is_empty()) {
        return;
    }
    RECT overlay_rect{};
    GetWindowRect(hwnd_, &overlay_rect);
    int const origin_x = overlay_rect.left;
    int const origin_y = overlay_rect.top;

    if (shift && ctrl) {
        core::RectPx const desktop_screen = Get_virtual_desktop_bounds_px();
        state_->live_rect =
            core::Screen_rect_to_client_rect(desktop_screen, origin_x, origin_y);
        state_->modifier_preview = true;
    } else if (shift) {
        core::PointPx const cursor_screen = Get_cursor_pos_px();
        std::optional<size_t> index =
            core::Index_of_monitor_containing(cursor_screen, state_->cached_monitors);
        if (index.has_value()) {
            state_->live_rect = core::Screen_rect_to_client_rect(
                state_->cached_monitors[*index].bounds, origin_x, origin_y);
        } else {
            state_->live_rect = {};
        }
        state_->modifier_preview = true;
    } else if (ctrl) {
        core::PointPx const cursor_screen = Get_cursor_pos_px();
        std::optional<core::RectPx> rect =
            Get_window_rect_under_cursor(To_point(cursor_screen), hwnd_);
        if (rect.has_value()) {
            state_->live_rect =
                core::Screen_rect_to_client_rect(*rect, origin_x, origin_y);
        } else {
            state_->live_rect = {};
        }
        state_->modifier_preview = true;
    } else {
        if (state_->modifier_preview) {
            state_->modifier_preview = false;
            state_->live_rect = {};
        }
    }
}

core::RectPx OverlayWindow::Selection_screen_rect() const {
    RECT overlay_rect{};
    GetWindowRect(hwnd_, &overlay_rect);
    core::RectPx const &sel = state_->final_selection;
    return core::RectPx::From_ltrb(sel.left + static_cast<int32_t>(overlay_rect.left),
                                   sel.top + static_cast<int32_t>(overlay_rect.top),
                                   sel.right + static_cast<int32_t>(overlay_rect.left),
                                   sel.bottom + static_cast<int32_t>(overlay_rect.top));
}

void OverlayWindow::Save_directly_and_close() {
    if (!resources_->capture.Is_valid()) {
        return;
    }
    core::RectPx const &selection = state_->final_selection;
    if (selection.Is_empty()) {
        return;
    }

    GdiCaptureResult cropped;
    if (!Crop_capture(resources_->capture, selection.left, selection.top,
                      selection.Width(), selection.Height(), cropped)) {
        Destroy();
        return;
    }

    std::wstring const save_dir = Resolve_default_save_directory();

    wchar_t default_name[256] = {};
    Build_default_save_name(save_dir, default_name, 256);

    // Determine extension from config.
    std::wstring_view ext = L".png";
    core::ImageSaveFormat format = core::ImageSaveFormat::Png;
    if (config_) {
        if (config_->default_save_format == L"jpg") {
            ext = L".jpg";
            format = core::ImageSaveFormat::Jpeg;
        } else if (config_->default_save_format == L"bmp") {
            ext = L".bmp";
            format = core::ImageSaveFormat::Bmp;
        }
    }

    std::wstring full_path = save_dir;
    if (!full_path.empty() && full_path.back() != L'\\') {
        full_path += L'\\';
    }
    full_path += default_name;
    full_path += ext;
    std::wstring const reserved_path = Reserve_unique_file_path(full_path);
    if (reserved_path.empty()) {
        cropped.Free();
        Destroy();
        return;
    }

    bool saved = false;
    if (format == core::ImageSaveFormat::Jpeg) {
        saved = Save_capture_to_jpeg(cropped, reserved_path.c_str());
    } else if (format == core::ImageSaveFormat::Bmp) {
        saved = Save_capture_to_bmp(cropped, reserved_path.c_str());
    } else {
        saved = Save_capture_to_png(cropped, reserved_path.c_str());
    }
    if (!saved) {
        (void)DeleteFileW(reserved_path.c_str());
    }

    HBITMAP thumb = saved ? Create_thumbnail_from_capture(cropped) : nullptr;
    cropped.Free();
    if (saved) {
        if (config_) {
            config_->default_save_dir = save_dir;
            config_->Normalize();
        }
        if (events_) {
            events_->On_selection_saved_to_file(Selection_screen_rect(),
                                                state_->selection_window, thumb);
            thumb = nullptr;
        }
        if (thumb != nullptr) {
            DeleteObject(thumb);
        }
    }
    Destroy();
}

void OverlayWindow::Save_as_and_close() {
    if (!resources_->capture.Is_valid()) {
        return;
    }
    core::RectPx const &selection = state_->final_selection;
    if (selection.Is_empty()) {
        return;
    }

    GdiCaptureResult cropped;
    if (!Crop_capture(resources_->capture, selection.left, selection.top,
                      selection.Width(), selection.Height(), cropped)) {
        Destroy();
        return;
    }

    std::wstring const initial_dir = Resolve_save_as_initial_directory();

    wchar_t default_name[256] = {};
    Build_default_save_name(initial_dir, default_name, 256);

    OPENFILENAMEW ofn = {};
    wchar_t path_buffer[MAX_PATH] = {};
    wcscpy_s(path_buffer, default_name);
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"PNG (*.png)\0*.png\0JPEG "
                      L"(*.jpg;*.jpeg)\0*.jpg;*.jpeg\0BMP (*.bmp)\0*.bmp\0\0";
    ofn.lpstrFile = path_buffer;
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

    std::wstring const resolved_path =
        core::Ensure_image_save_extension(path_buffer, ofn.nFilterIndex);
    wcsncpy_s(path_buffer, MAX_PATH, resolved_path.c_str(), _TRUNCATE);

    bool saved = false;
    core::ImageSaveFormat const format =
        core::Detect_image_save_format_from_path(path_buffer);
    if (format == core::ImageSaveFormat::Jpeg) {
        saved = Save_capture_to_jpeg(cropped, path_buffer);
    } else if (format == core::ImageSaveFormat::Bmp) {
        saved = Save_capture_to_bmp(cropped, path_buffer);
    } else {
        saved = Save_capture_to_png(cropped, path_buffer);
    }

    HBITMAP thumb = saved ? Create_thumbnail_from_capture(cropped) : nullptr;
    cropped.Free();
    if (saved) {
        wchar_t *last_slash = wcsrchr(path_buffer, L'\\');
        if (last_slash && config_) {
            size_t const dir_len = static_cast<size_t>(last_slash - path_buffer);
            if (dir_len < MAX_PATH) {
                config_->last_save_as_dir.assign(path_buffer, dir_len);
                config_->Normalize();
            }
        }
        if (events_) {
            events_->On_selection_saved_to_file(Selection_screen_rect(),
                                                state_->selection_window, thumb);
            thumb = nullptr;
        }
        if (thumb != nullptr) {
            DeleteObject(thumb);
        }
        Destroy();
    }
}

void OverlayWindow::Copy_to_clipboard_and_close() {
    if (!resources_->capture.Is_valid()) {
        return;
    }
    core::RectPx const &selection = state_->final_selection;
    if (selection.Is_empty()) {
        return;
    }

    GdiCaptureResult cropped;
    if (!Crop_capture(resources_->capture, selection.left, selection.top,
                      selection.Width(), selection.Height(), cropped)) {
        Destroy();
        return;
    }
    bool const copied_to_clipboard = Copy_capture_to_clipboard(cropped, hwnd_);
    cropped.Free();

    if (copied_to_clipboard && events_) {
        events_->On_selection_copied_to_clipboard(Selection_screen_rect(),
                                                  state_->selection_window);
    }
    Destroy();
}

LRESULT OverlayWindow::On_key_down(WPARAM wparam, LPARAM lparam) {
    if (wparam == VK_ESCAPE) {
        if (state_->move_dragging) {
            state_->final_selection = state_->move_anchor_rect;
            state_->move_dragging = false;
            state_->live_rect = {};
            InvalidateRect(hwnd_, nullptr, TRUE);
        } else if (state_->handle_dragging) {
            state_->handle_dragging = false;
            state_->resize_handle = std::nullopt;
            state_->live_rect = {};
            InvalidateRect(hwnd_, nullptr, TRUE);
        } else if (state_->dragging) {
            state_->dragging = false;
            state_->live_rect = {};
            InvalidateRect(hwnd_, nullptr, TRUE);
        } else if (!state_->final_selection.Is_empty()) {
            state_->final_selection = {};
            state_->live_rect = {};
            InvalidateRect(hwnd_, nullptr, TRUE);
        } else {
            Destroy();
        }
        return 0;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
        !state_->final_selection.Is_empty()) {
        if (wparam == L'S') {
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                Save_as_and_close();
            } else {
                Save_directly_and_close();
            }
            return 0;
        }
        if (wparam == L'C') {
            Copy_to_clipboard_and_close();
            return 0;
        }
    }
    if (wparam == VK_SHIFT || wparam == VK_CONTROL) {
        if (!state_->dragging && !state_->handle_dragging) {
            bool const shift =
                (wparam == VK_SHIFT) || (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool const ctrl =
                (wparam == VK_CONTROL) || (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            Update_modifier_preview(shift, ctrl);
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
        return 0;
    }
    if (wparam == VK_MENU) {
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }
    return DefWindowProcW(hwnd_, WM_KEYDOWN, wparam, lparam);
}

LRESULT OverlayWindow::On_key_up(WPARAM wparam, LPARAM lparam) {
    if (wparam == VK_MENU) {
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }
    if (wparam != VK_SHIFT && wparam != VK_CONTROL) {
        return DefWindowProcW(hwnd_, WM_KEYUP, wparam, lparam);
    }
    if (state_->modifier_preview) {
        state_->modifier_preview = false;
        state_->live_rect = {};
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
    return 0;
}

LRESULT OverlayWindow::On_l_button_down() {
    if (!resources_->capture.Is_valid()) {
        return 0;
    }
    bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if ((shift || ctrl) && state_->modifier_preview) {
        state_->final_selection = state_->live_rect;
        state_->selection_window = std::nullopt;
        state_->selection_monitor_index = std::nullopt;
        if (shift && ctrl) {
            state_->selection_source = core::SaveSelectionSource::Desktop;
        } else if (shift) {
            state_->selection_source = core::SaveSelectionSource::Monitor;
            core::PointPx const cursor_screen = Get_cursor_pos_px();
            std::optional<size_t> index = core::Index_of_monitor_containing(
                cursor_screen, state_->cached_monitors);
            if (index.has_value()) {
                state_->selection_monitor_index = *index;
            }
        } else {
            state_->selection_source = core::SaveSelectionSource::Window;
            core::PointPx const cursor_screen = Get_cursor_pos_px();
            std::optional<HWND> window =
                Get_window_under_cursor(To_point(cursor_screen), hwnd_);
            if (window.has_value()) {
                state_->selection_window = *window;
            }
        }
        state_->modifier_preview = false;
        state_->live_rect = {};
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }

    core::PointPx const cursor = Get_client_cursor_pos_px(hwnd_);
    if (!state_->final_selection.Is_empty() && !state_->dragging &&
        !state_->handle_dragging && !state_->move_dragging) {
        bool const tab_held = (GetKeyState(VK_TAB) & 0x8000) != 0;
        if (tab_held && state_->final_selection.Contains(cursor)) {
            state_->move_dragging = true;
            state_->move_grab_offset = {cursor.x - state_->final_selection.left,
                                        cursor.y - state_->final_selection.top};
            state_->move_anchor_rect = state_->final_selection;
            state_->live_rect = state_->final_selection;
            Build_snap_edges_from_windows();
            InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        }
        std::optional<core::SelectionHandle> hit = core::Hit_test_selection_handle(
            state_->final_selection, cursor, kHandleGrabRadiusPx);
        if (hit.has_value()) {
            state_->handle_dragging = true;
            state_->resize_handle = hit;
            state_->resize_anchor_rect = state_->final_selection;
            state_->live_rect = state_->final_selection;
            Build_snap_edges_from_windows();
            InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        }
        return 0;
    }

    Build_snap_edges_from_windows();
    core::PointPx snapped_start = cursor;
    if (Is_snap_enabled()) {
        snapped_start = core::Snap_point_to_edges(
            cursor, state_->vertical_edges, state_->horizontal_edges, kSnapThresholdPx);
    }
    state_->start_px = snapped_start;
    state_->dragging = true;
    state_->final_selection = {};
    state_->selection_source = core::SaveSelectionSource::Region;
    state_->selection_window = std::nullopt;
    state_->selection_monitor_index = std::nullopt;
    state_->live_rect = core::RectPx::From_points(state_->start_px, state_->start_px);
    InvalidateRect(hwnd_, nullptr, TRUE);
    return 0;
}

LRESULT OverlayWindow::On_mouse_move() {
    if (state_->move_dragging) {
        core::PointPx const cursor = Get_client_cursor_pos_px(hwnd_);
        int32_t const new_left = cursor.x - state_->move_grab_offset.x;
        int32_t const new_top = cursor.y - state_->move_grab_offset.y;
        core::RectPx candidate = core::RectPx::From_ltrb(
            new_left, new_top, new_left + state_->move_anchor_rect.Width(),
            new_top + state_->move_anchor_rect.Height());
        if (Is_snap_enabled()) {
            candidate = core::Snap_moved_rect_to_edges(
                candidate, state_->vertical_edges, state_->horizontal_edges,
                kSnapThresholdPx);
        }
        state_->live_rect = candidate;
    } else if (state_->handle_dragging && state_->resize_handle.has_value()) {
        core::PointPx const cursor = Get_client_cursor_pos_px(hwnd_);
        core::RectPx candidate = core::Resize_rect_from_handle(
            state_->resize_anchor_rect, *state_->resize_handle, cursor);
        if (Is_snap_enabled()) {
            candidate =
                core::Snap_rect_to_edges(candidate, state_->vertical_edges,
                                         state_->horizontal_edges, kSnapThresholdPx);
        }
        core::PointPx const anchor = core::Anchor_point_for_resize_policy(
            state_->resize_anchor_rect, *state_->resize_handle);
        state_->live_rect =
            core::Allowed_selection_rect(candidate, anchor, state_->cached_monitors);
    } else if (state_->dragging) {
        core::PointPx const cursor = Get_client_cursor_pos_px(hwnd_);
        core::RectPx candidate =
            core::RectPx::From_points(state_->start_px, cursor).Normalized();
        if (Is_snap_enabled()) {
            candidate =
                core::Snap_rect_to_edges(candidate, state_->vertical_edges,
                                         state_->horizontal_edges, kSnapThresholdPx);
        }
        state_->live_rect = candidate;
    } else {
        bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        Update_modifier_preview(shift, ctrl);
    }

    ULONGLONG const now = GetTickCount64();
    if (now - state_->last_invalidate_tick >= 16) {
        state_->last_invalidate_tick = now;
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
    return 0;
}

LRESULT OverlayWindow::On_l_button_up() {
    if (state_->move_dragging) {
        core::RectPx to_commit = state_->live_rect;
        if (Is_snap_enabled()) {
            to_commit = core::Snap_moved_rect_to_edges(
                to_commit, state_->vertical_edges, state_->horizontal_edges,
                kSnapThresholdPx);
        }
        core::PointPx const center = {to_commit.left + to_commit.Width() / 2,
                                      to_commit.top + to_commit.Height() / 2};
        state_->final_selection =
            core::Allowed_selection_rect(to_commit, center, state_->cached_monitors);
        state_->move_dragging = false;
        state_->live_rect = {};
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }

    if (state_->handle_dragging && state_->resize_handle.has_value()) {
        core::RectPx to_commit = state_->live_rect;
        if (Is_snap_enabled()) {
            to_commit =
                core::Snap_rect_to_edges(to_commit, state_->vertical_edges,
                                         state_->horizontal_edges, kSnapThresholdPx);
        }
        core::PointPx const anchor = core::Anchor_point_for_resize_policy(
            state_->resize_anchor_rect, *state_->resize_handle);
        state_->final_selection =
            core::Allowed_selection_rect(to_commit, anchor, state_->cached_monitors);
        state_->handle_dragging = false;
        state_->resize_handle = std::nullopt;
        state_->live_rect = {};
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }

    if (state_->dragging) {
        core::PointPx const cursor = Get_client_cursor_pos_px(hwnd_);
        core::RectPx raw =
            core::RectPx::From_points(state_->start_px, cursor).Normalized();
        if (Is_snap_enabled()) {
            raw = core::Snap_rect_to_edges(raw, state_->vertical_edges,
                                           state_->horizontal_edges, kSnapThresholdPx);
        }
        state_->final_selection = core::Allowed_selection_rect(raw, state_->start_px,
                                                               state_->cached_monitors);
        state_->selection_source = core::SaveSelectionSource::Region;
        state_->selection_window = std::nullopt;
        state_->selection_monitor_index = std::nullopt;
        state_->dragging = false;
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
    return 0;
}

LRESULT OverlayWindow::On_paint() {
    PAINTSTRUCT paint{};
    HDC const hdc = BeginPaint(hwnd_, &paint);
    if (hdc) {
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        PaintOverlayInput input{};
        input.capture = &resources_->capture;
        input.dragging = state_->dragging;
        input.handle_dragging = state_->handle_dragging;
        input.move_dragging = state_->move_dragging;
        input.modifier_preview = state_->modifier_preview;
        input.live_rect = state_->live_rect;
        input.final_selection = state_->final_selection;
        core::PointPx cursor = Get_client_cursor_pos_px(hwnd_);
        bool const crosshair_mode = state_->final_selection.Is_empty() &&
                                    !state_->dragging && !state_->handle_dragging &&
                                    !state_->modifier_preview;
        if (crosshair_mode && Is_snap_enabled()) {
            cursor =
                core::Snap_point_to_edges(cursor, state_->vertical_edges,
                                          state_->horizontal_edges, kSnapThresholdPx);
            // Monitor right/bottom snap edges are exclusive boundaries
            // (e.g. x == width) but pixel indices are zero-based, so clamp
            // to the last valid pixel to keep crosshair/magnifier visible.
            if (cursor.x >= rect.right) cursor.x = rect.right - 1;
            if (cursor.y >= rect.bottom) cursor.y = rect.bottom - 1;
        }
        input.cursor_client_px = cursor;
        input.paint_buffer = std::span<uint8_t>(resources_->paint_buffer);
        input.resources = &resources_->paint;
        Paint_overlay(hdc, hwnd_, rect, input);
        EndPaint(hwnd_, &paint);
    }
    return 0;
}

LRESULT OverlayWindow::On_destroy() {
    resources_->Reset();
    state_->Reset_for_session();
    if (events_) {
        events_->On_overlay_closed();
    }
    return 0;
}

LRESULT OverlayWindow::On_close() {
    Destroy();
    return 0;
}

LRESULT OverlayWindow::On_set_cursor(WPARAM wparam, LPARAM lparam) {
    if (LOWORD(lparam) != HTCLIENT) {
        return DefWindowProcW(hwnd_, WM_SETCURSOR, wparam, lparam);
    }
    if (state_->move_dragging) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return TRUE;
    }
    if (state_->handle_dragging && state_->resize_handle.has_value()) {
        SetCursor(Cursor_for_handle(*state_->resize_handle));
        return TRUE;
    }
    if (state_->modifier_preview) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    }
    if (!state_->final_selection.Is_empty() && !state_->dragging) {
        core::PointPx const cursor = Get_client_cursor_pos_px(hwnd_);
        bool const tab_held = (GetKeyState(VK_TAB) & 0x8000) != 0;
        if (tab_held && state_->final_selection.Contains(cursor)) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        std::optional<core::SelectionHandle> hit = core::Hit_test_selection_handle(
            state_->final_selection, cursor, kHandleGrabRadiusPx);
        if (hit.has_value()) {
            SetCursor(Cursor_for_handle(*hit));
            return TRUE;
        }
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    }
    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
    return TRUE;
}

} // namespace greenflame
