#include "greenflame_app.h"

#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"
#include "win/display_queries.h"
#include "win/gdi_capture.h"
#include "win/window_query.h"

namespace {

constexpr wchar_t kClipboardCopiedBalloonMessage[] = L"Selection copied to clipboard.";
constexpr wchar_t kSelectionSavedBalloonMessage[] = L"Selection saved.";
constexpr wchar_t kNoLastRegionMessage[] = L"No previously captured region.";
constexpr wchar_t kNoLastWindowMessage[] = L"No previously captured window.";
constexpr wchar_t kLastWindowClosedMessage[] =
    L"Previously captured window is no longer available.";
constexpr wchar_t kLastWindowMinimizedMessage[] =
    L"Previously captured window is minimized.";
constexpr int kThumbnailMaxWidth = 320;
constexpr int kThumbnailMaxHeight = 120;

[[nodiscard]] HBITMAP Create_thumbnail_from_clipboard() {
    if (OpenClipboard(nullptr) == 0) {
        return nullptr;
    }
    HBITMAP clip_bmp = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    if (clip_bmp == nullptr) {
        CloseClipboard();
        return nullptr;
    }
    BITMAP bm{};
    if (GetObject(clip_bmp, sizeof(bm), &bm) == 0 || bm.bmWidth <= 0 ||
        bm.bmHeight <= 0) {
        CloseClipboard();
        return nullptr;
    }
    float const scale_w = static_cast<float>(kThumbnailMaxWidth) / bm.bmWidth;
    float const scale_h = static_cast<float>(kThumbnailMaxHeight) / bm.bmHeight;
    float scale = (std::min)(scale_w, scale_h);
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    int thumb_w = static_cast<int>(bm.bmWidth * scale);
    int thumb_h = static_cast<int>(bm.bmHeight * scale);
    if (thumb_w <= 0) {
        thumb_w = 1;
    }
    if (thumb_h <= 0) {
        thumb_h = 1;
    }
    HBITMAP result = nullptr;
    HDC const screen_dc = GetDC(nullptr);
    if (screen_dc != nullptr) {
        HDC const src_dc = CreateCompatibleDC(screen_dc);
        HDC const dst_dc = CreateCompatibleDC(screen_dc);
        result = CreateCompatibleBitmap(screen_dc, thumb_w, thumb_h);
        if (src_dc != nullptr && dst_dc != nullptr && result != nullptr) {
            HGDIOBJ const old_src = SelectObject(src_dc, clip_bmp);
            HGDIOBJ const old_dst = SelectObject(dst_dc, result);
            SetStretchBltMode(dst_dc, HALFTONE);
            SetBrushOrgEx(dst_dc, 0, 0, nullptr);
            StretchBlt(dst_dc, 0, 0, thumb_w, thumb_h, src_dc, 0, 0, bm.bmWidth,
                       bm.bmHeight, SRCCOPY);
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
    CloseClipboard();
    return result;
}

void Enable_per_monitor_dpi_awareness_v2() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }
    using SetDpiAwarenessContextFn =
        DPI_AWARENESS_CONTEXT(WINAPI *)(DPI_AWARENESS_CONTEXT);
    auto fn = reinterpret_cast<SetDpiAwarenessContextFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (fn) {
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

#if defined(_DEBUG)
constexpr wchar_t kTestingFlag[] = L"--testing-1-2";

[[nodiscard]] bool Is_testing_mode_enabled() {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return false;
    }

    bool testing_mode_enabled = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], kTestingFlag) == 0) {
            testing_mode_enabled = true;
            break;
        }
    }

    LocalFree(argv);
    return testing_mode_enabled;
}
#else
[[nodiscard]] bool Is_testing_mode_enabled() { return false; }
#endif

[[nodiscard]] bool Copy_screen_rect_to_clipboard(greenflame::core::RectPx screen_rect) {
    if (screen_rect.Is_empty()) {
        return false;
    }

    greenflame::core::RectPx const virtual_bounds =
        greenflame::Get_virtual_desktop_bounds_px();
    std::optional<greenflame::core::RectPx> const clipped_screen =
        greenflame::core::RectPx::Clip(screen_rect, virtual_bounds);
    if (!clipped_screen.has_value()) {
        return false;
    }

    greenflame::GdiCaptureResult capture{};
    if (!greenflame::Capture_virtual_desktop(capture)) {
        return false;
    }

    greenflame::core::RectPx const capture_rect = greenflame::core::RectPx::From_ltrb(
        clipped_screen->left - virtual_bounds.left,
        clipped_screen->top - virtual_bounds.top,
        clipped_screen->right - virtual_bounds.left,
        clipped_screen->bottom - virtual_bounds.top);
    greenflame::GdiCaptureResult cropped{};
    bool const cropped_ok =
        greenflame::Crop_capture(capture, capture_rect.left, capture_rect.top,
                                 capture_rect.Width(), capture_rect.Height(), cropped);
    capture.Free();
    if (!cropped_ok) {
        return false;
    }

    bool const copied = greenflame::Copy_capture_to_clipboard(cropped, nullptr);
    cropped.Free();
    return copied;
}

struct WindowCaptureResult {
    HWND hwnd;
    greenflame::core::RectPx screen_rect;
};

[[nodiscard]] std::optional<WindowCaptureResult>
Find_and_capture_current_window(HWND target_window) {
    if (target_window != nullptr) {
        std::optional<greenflame::core::RectPx> const target_rect =
            greenflame::Get_window_rect(target_window);
        if (target_rect.has_value()) {
            if (Copy_screen_rect_to_clipboard(*target_rect)) {
                return WindowCaptureResult{target_window, *target_rect};
            }
            return std::nullopt;
        }
    }

    HWND fg = GetForegroundWindow();
    if (fg != nullptr) {
        std::optional<greenflame::core::RectPx> const fg_rect =
            greenflame::Get_window_rect(fg);
        if (fg_rect.has_value()) {
            if (Copy_screen_rect_to_clipboard(*fg_rect)) {
                return WindowCaptureResult{fg, *fg_rect};
            }
            return std::nullopt;
        }
    }

    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return std::nullopt;
    }
    std::optional<HWND> const under_cursor =
        greenflame::Get_window_under_cursor(cursor, nullptr);
    if (!under_cursor.has_value()) {
        return std::nullopt;
    }
    std::optional<greenflame::core::RectPx> const fallback_rect =
        greenflame::Get_window_rect(*under_cursor);
    if (!fallback_rect.has_value()) {
        return std::nullopt;
    }
    if (Copy_screen_rect_to_clipboard(*fallback_rect)) {
        return WindowCaptureResult{*under_cursor, *fallback_rect};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<greenflame::core::RectPx>
Copy_current_monitor_to_clipboard() {
    std::vector<greenflame::core::MonitorWithBounds> const monitors =
        greenflame::Get_monitors_with_bounds();
    if (monitors.empty()) {
        return std::nullopt;
    }

    greenflame::core::PointPx const cursor = greenflame::Get_cursor_pos_px();
    std::optional<size_t> const index =
        greenflame::core::Index_of_monitor_containing(cursor, monitors);
    if (!index.has_value()) {
        return std::nullopt;
    }
    greenflame::core::RectPx const rect = monitors[*index].bounds;
    if (Copy_screen_rect_to_clipboard(rect)) {
        return rect;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<greenflame::core::RectPx> Copy_desktop_to_clipboard() {
    greenflame::core::RectPx const rect = greenflame::Get_virtual_desktop_bounds_px();
    if (Copy_screen_rect_to_clipboard(rect)) {
        return rect;
    }
    return std::nullopt;
}

} // namespace

namespace greenflame {

GreenflameApp::GreenflameApp(HINSTANCE hinstance)
    : hinstance_(hinstance), tray_window_(this), overlay_window_(this, &config_) {}

int GreenflameApp::Run() {
    Enable_per_monitor_dpi_awareness_v2();
    if (!OverlayWindow::Register_window_class(hinstance_) ||
        !TrayWindow::Register_window_class(hinstance_)) {
        return 1;
    }

    config_ = AppConfig::Load();
    bool const testing_mode_enabled = Is_testing_mode_enabled();
    if (!tray_window_.Create(hinstance_, testing_mode_enabled)) {
        return 2;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    (void)config_.Save();
    return static_cast<int>(message.wParam);
}

void GreenflameApp::On_start_capture_requested() {
    (void)overlay_window_.Create_and_show(hinstance_);
}

void GreenflameApp::On_copy_window_to_clipboard_requested(HWND target_window) {
    if (overlay_window_.Is_open()) {
        return;
    }
    std::optional<WindowCaptureResult> const result =
        Find_and_capture_current_window(target_window);
    if (result.has_value()) {
        Store_last_capture(result->screen_rect, result->hwnd);
        if (config_.show_balloons) {
            tray_window_.Show_balloon(TrayBalloonIcon::Info,
                                      kClipboardCopiedBalloonMessage,
                                      Create_thumbnail_from_clipboard());
        }
    }
}

void GreenflameApp::On_copy_monitor_to_clipboard_requested() {
    if (overlay_window_.Is_open()) {
        return;
    }
    std::optional<core::RectPx> const rect = Copy_current_monitor_to_clipboard();
    if (rect.has_value()) {
        Store_last_capture(*rect, std::nullopt);
        if (config_.show_balloons) {
            tray_window_.Show_balloon(TrayBalloonIcon::Info,
                                      kClipboardCopiedBalloonMessage,
                                      Create_thumbnail_from_clipboard());
        }
    }
}

void GreenflameApp::On_copy_desktop_to_clipboard_requested() {
    if (overlay_window_.Is_open()) {
        return;
    }
    std::optional<core::RectPx> const rect = Copy_desktop_to_clipboard();
    if (rect.has_value()) {
        Store_last_capture(*rect, std::nullopt);
        if (config_.show_balloons) {
            tray_window_.Show_balloon(TrayBalloonIcon::Info,
                                      kClipboardCopiedBalloonMessage,
                                      Create_thumbnail_from_clipboard());
        }
    }
}

void GreenflameApp::On_copy_last_region_to_clipboard_requested() {
    if (overlay_window_.Is_open()) {
        return;
    }
    if (!last_capture_screen_rect_.has_value()) {
        tray_window_.Show_balloon(TrayBalloonIcon::Warning, kNoLastRegionMessage);
        return;
    }
    if (Copy_screen_rect_to_clipboard(*last_capture_screen_rect_)) {
        if (config_.show_balloons) {
            tray_window_.Show_balloon(TrayBalloonIcon::Info,
                                      kClipboardCopiedBalloonMessage,
                                      Create_thumbnail_from_clipboard());
        }
    } else {
        tray_window_.Show_balloon(TrayBalloonIcon::Warning, kNoLastRegionMessage);
    }
}

void GreenflameApp::On_copy_last_window_to_clipboard_requested() {
    if (overlay_window_.Is_open()) {
        return;
    }
    if (!last_capture_window_.has_value()) {
        tray_window_.Show_balloon(TrayBalloonIcon::Warning, kNoLastWindowMessage);
        return;
    }
    HWND const hwnd = *last_capture_window_;
    if (!IsWindow(hwnd)) {
        tray_window_.Show_balloon(TrayBalloonIcon::Warning, kLastWindowClosedMessage);
        last_capture_window_ = std::nullopt;
        return;
    }
    if (IsIconic(hwnd) != 0) {
        tray_window_.Show_balloon(TrayBalloonIcon::Warning,
                                  kLastWindowMinimizedMessage);
        return;
    }
    std::optional<core::RectPx> const rect = Get_window_rect(hwnd);
    if (!rect.has_value()) {
        tray_window_.Show_balloon(TrayBalloonIcon::Warning, kLastWindowClosedMessage);
        last_capture_window_ = std::nullopt;
        return;
    }
    if (Copy_screen_rect_to_clipboard(*rect)) {
        Store_last_capture(*rect, hwnd);
        if (config_.show_balloons) {
            tray_window_.Show_balloon(TrayBalloonIcon::Info,
                                      kClipboardCopiedBalloonMessage,
                                      Create_thumbnail_from_clipboard());
        }
    }
}

void GreenflameApp::On_exit_requested() {
    overlay_window_.Destroy();
    tray_window_.Destroy();
}

void GreenflameApp::On_overlay_closed() {
    // Overlay lifecycle is managed by OverlayWindow; no app action needed.
}

void GreenflameApp::On_selection_copied_to_clipboard(core::RectPx screen_rect,
                                                     std::optional<HWND> window) {
    Store_last_capture(screen_rect, window);
    if (config_.show_balloons) {
        tray_window_.Show_balloon(TrayBalloonIcon::Info,
                                  kClipboardCopiedBalloonMessage,
                                  Create_thumbnail_from_clipboard());
    }
}

void GreenflameApp::On_selection_saved_to_file(core::RectPx screen_rect,
                                               std::optional<HWND> window,
                                               HBITMAP thumbnail) {
    Store_last_capture(screen_rect, window);
    if (config_.show_balloons) {
        tray_window_.Show_balloon(TrayBalloonIcon::Info, kSelectionSavedBalloonMessage,
                                  thumbnail);
    } else if (thumbnail != nullptr) {
        DeleteObject(thumbnail);
    }
}

void GreenflameApp::Store_last_capture(core::RectPx screen_rect,
                                       std::optional<HWND> window) {
    last_capture_screen_rect_ = screen_rect;
    if (window.has_value()) {
        last_capture_window_ = *window;
    }
}

} // namespace greenflame
