// Overlay window object: fullscreen capture/selection UI.

#include "win/overlay_window.h"

#include "greenflame_core/app_config.h"
#include "greenflame_core/modification_command.h"
#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/save_image_policy.h"
#include "greenflame_core/selection_handles.h"
#include "greenflame_core/snap_to_edges.h"
#include "greenflame_core/window_query.h"
#include "win/display_queries.h"
#include "win/gdi_capture.h"
#include "win/overlay_paint.h"
#include "win/save_image.h"
#include "win/ui_palette.h"

namespace {

constexpr wchar_t kOverlayWindowClass[] = L"GreenflameOverlay";
constexpr int32_t kSnapThresholdPx = 10;
constexpr int kDimensionFontHeight = 14;
constexpr int kCenterFontHeight = 36;

constexpr int kThumbnailMaxWidth = 320;
constexpr int kThumbnailMaxHeight = 120;

[[nodiscard]] HBITMAP
Create_thumbnail_from_capture(greenflame::GdiCaptureResult const &capture) {
    if (!capture.Is_valid()) {
        return nullptr;
    }

    float const scale_w =
        static_cast<float>(kThumbnailMaxWidth) / static_cast<float>(capture.width);
    float const scale_h =
        static_cast<float>(kThumbnailMaxHeight) / static_cast<float>(capture.height);
    float scale = (std::min)(scale_w, scale_h);
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    int tw = static_cast<int>(static_cast<float>(capture.width) * scale);
    int th = static_cast<int>(static_cast<float>(capture.height) * scale);
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
        paint.crosshair_pen = CreatePen(PS_SOLID, 1, kOverlayCrosshair);
        paint.border_pen = CreatePen(PS_SOLID, 1, kBorderColor);
        paint.handle_pen = CreatePen(PS_SOLID, 1, kOverlayHandle);
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
        if (paint.handle_pen) {
            DeleteObject(paint.handle_pen);
            paint.handle_pen = nullptr;
        }
    }
};

OverlayWindow::OverlayWindow(IOverlayEvents *events, core::AppConfig *config,
                             IWindowQuery *window_query)
    : events_(events), config_(config), window_query_(window_query),
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
    if (window_query_ == nullptr) {
        return false;
    }
    hinstance_ = hinstance;
    resources_->Reset();

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

    controller_.Reset_for_session(Get_monitors_with_bounds());
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

void OverlayWindow::Apply_action(core::OverlayAction action) {
    switch (action) {
    case core::OverlayAction::Repaint:
        InvalidateRect(hwnd_, nullptr, TRUE);
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
    bool const eff_ctrl = ctrl || (wparam == VK_CONTROL);
    bool const eff_shift = shift || (wparam == VK_SHIFT);
    bool const eff_alt = alt || (wparam == VK_MENU);

    if (wparam == VK_ESCAPE) {
        Apply_action(controller_.On_cancel());
        return 0;
    }
    if (eff_ctrl && wparam == L'Z') {
        if (eff_shift) {
            controller_.Redo();
        } else {
            controller_.Undo();
        }
        InvalidateRect(hwnd_, nullptr, TRUE);
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

LRESULT OverlayWindow::On_l_button_down() {
    if (!resources_->capture.Is_valid()) {
        return 0;
    }
    bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool const alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool const tab = (GetKeyState(VK_TAB) & 0x8000) != 0;
    core::OverlayModifierState mods{shift, ctrl, alt, tab};
    core::PointPx const cursor_client = Get_client_cursor_pos_px(hwnd_);
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

    // Collect all rects for snap-edge rebuild (window rects + monitor bounds).
    std::vector<core::RectPx> vis_rects;
    window_query_->Get_visible_top_level_window_rects(hwnd_, vis_rects);
    for (auto const &m : controller_.State().cached_monitors) {
        vis_rects.push_back(m.bounds);
    }

    Apply_action(controller_.On_primary_press(
        mods, cursor_client, cursor_screen, win_handle, monitor_idx,
        std::optional<core::RectPx>{}, vdesk, std::move(vis_rects), wr.left, wr.top));
    return 0;
}

LRESULT OverlayWindow::On_mouse_move() {
    bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool const alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    core::OverlayModifierState mods{shift, ctrl, alt};
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
    Apply_action(controller_.On_pointer_move(mods, cursor_client, cursor_screen,
                                             win_rect, vdesk, monitor_idx, ox, oy,
                                             static_cast<uint64_t>(GetTickCount64())));

    auto const &s2 = controller_.State();
    if (!s2.final_selection.Is_empty() && !s2.dragging && !s2.handle_dragging &&
        !s2.move_dragging) {
        core::PointPx const cur = Get_client_cursor_pos_px(hwnd_);
        bool const tab_held = (GetKeyState(VK_TAB) & 0x8000) != 0;
        std::optional<core::SelectionHandle> hover =
            (tab_held && s2.final_selection.Contains(cur))
                ? std::nullopt
                : core::Hit_test_border_zone(s2.final_selection, cur);
        if (hover != last_hover_handle_) {
            last_hover_handle_ = hover;
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
    } else if (last_hover_handle_.has_value()) {
        last_hover_handle_ = std::nullopt;
        // no extra repaint needed — drags already repaint
    }

    return 0;
}

LRESULT OverlayWindow::On_l_button_up() {
    auto const &s = controller_.State();
    bool const was_move = s.move_dragging;
    bool const was_resize = s.handle_dragging;
    core::RectPx const before =
        was_move ? s.move_anchor_rect
                 : (was_resize ? s.resize_anchor_rect : core::RectPx{});

    core::OverlayModifierState mods{};
    mods.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    Apply_action(controller_.On_primary_release(mods, Get_client_cursor_pos_px(hwnd_)));

    if ((was_move || was_resize)) {
        core::RectPx const after = controller_.State().final_selection;
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
    }

    return 0;
}

LRESULT OverlayWindow::Wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        return On_key_down(wparam, lparam);
    case WM_KEYUP:
    case WM_SYSKEYUP:
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

    std::wstring const save_dir = Resolve_default_save_directory();

    wchar_t default_name[256] = {};
    Build_default_save_name(save_dir, default_name);

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

    bool saved = false;
    core::ImageSaveFormat const format =
        core::Detect_image_save_format_from_path(std::wstring_view(path_buffer.data()));
    if (format == core::ImageSaveFormat::Jpeg) {
        saved = Save_capture_to_jpeg(cropped, path_buffer.data());
    } else if (format == core::ImageSaveFormat::Bmp) {
        saved = Save_capture_to_bmp(cropped, path_buffer.data());
    } else {
        saved = Save_capture_to_png(cropped, path_buffer.data());
    }

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
    bool const copied_to_clipboard = Copy_capture_to_clipboard(cropped, hwnd_);
    cropped.Free();

    if (copied_to_clipboard && events_) {
        events_->On_selection_copied_to_clipboard(Selection_screen_rect(),
                                                  controller_.State().selection_window);
    }
    Destroy();
}

LRESULT OverlayWindow::On_paint() {
    PAINTSTRUCT paint{};
    HDC const hdc = BeginPaint(hwnd_, &paint);
    if (hdc) {
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        auto const &s = controller_.State();
        PaintOverlayInput input{};
        input.capture = &resources_->capture;
        input.dragging = s.dragging;
        input.handle_dragging = s.handle_dragging;
        input.move_dragging = s.move_dragging;
        input.modifier_preview = s.modifier_preview;
        if (config_ != nullptr) {
            input.show_selection_size_side_labels =
                config_->show_selection_size_side_labels;
            input.show_selection_size_center_label =
                config_->show_selection_size_center_label;
        }
        input.live_rect = s.live_rect;
        input.final_selection = s.final_selection;
        core::PointPx cursor = Get_client_cursor_pos_px(hwnd_);
        bool const snap_enabled = (GetKeyState(VK_MENU) & 0x8000) == 0;
        bool const crosshair_mode = s.final_selection.Is_empty() && !s.dragging &&
                                    !s.handle_dragging && !s.modifier_preview;
        if (crosshair_mode && snap_enabled) {
            cursor = core::Snap_point_to_edges(cursor, s.vertical_edges,
                                               s.horizontal_edges, kSnapThresholdPx);
            // Monitor right/bottom snap edges are exclusive boundaries
            // (e.g. x == width) but pixel indices are zero-based, so clamp
            // to the last valid pixel to keep crosshair/magnifier visible.
            if (cursor.x >= rect.right) cursor.x = rect.right - 1;
            if (cursor.y >= rect.bottom) cursor.y = rect.bottom - 1;
        }
        input.cursor_client_px = cursor;
        input.paint_buffer = std::span<uint8_t>(resources_->paint_buffer);
        input.resources = &resources_->paint;
        if (s.handle_dragging && s.resize_handle.has_value()) {
            input.highlight_handle = s.resize_handle;
        } else if (!s.move_dragging && !s.dragging && !s.modifier_preview) {
            input.highlight_handle = last_hover_handle_;
        }
        Paint_overlay(hdc, hwnd_, rect, input);
        EndPaint(hwnd_, &paint);
    }
    return 0;
}

LRESULT OverlayWindow::On_destroy() {
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

LRESULT OverlayWindow::On_set_cursor(WPARAM wparam, LPARAM lparam) {
    if (LOWORD(lparam) != HTCLIENT) {
        return DefWindowProcW(hwnd_, WM_SETCURSOR, wparam, lparam);
    }
    auto const &s = controller_.State();
    if (s.move_dragging) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return TRUE;
    }
    if (s.handle_dragging && s.resize_handle.has_value()) {
        SetCursor(Cursor_for_handle(*s.resize_handle));
        return TRUE;
    }
    if (s.modifier_preview) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    }
    if (!s.final_selection.Is_empty() && !s.dragging) {
        core::PointPx const cursor = Get_client_cursor_pos_px(hwnd_);
        bool const tab_held = (GetKeyState(VK_TAB) & 0x8000) != 0;
        if (tab_held && s.final_selection.Contains(cursor)) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        std::optional<core::SelectionHandle> hit =
            core::Hit_test_border_zone(s.final_selection, cursor);
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
