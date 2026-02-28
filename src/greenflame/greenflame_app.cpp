#include "greenflame_app.h"
#include "app_config_store.h"

namespace {

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

    float const scale_w =
        static_cast<float>(kThumbnailMaxWidth) / static_cast<float>(bm.bmWidth);
    float const scale_h =
        static_cast<float>(kThumbnailMaxHeight) / static_cast<float>(bm.bmHeight);
    float scale = (std::min)(scale_w, scale_h);
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    int thumb_w = static_cast<int>(static_cast<float>(bm.bmWidth) * scale);
    int thumb_h = static_cast<int>(static_cast<float>(bm.bmHeight) * scale);
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
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

[[nodiscard]] bool
Is_testing_mode_enabled(greenflame::core::CliOptions const &options) {
#ifdef DEBUG
    return options.testing_1_2;
#else
    (void)options;
    return false;
#endif
}

void Write_console_text(std::wstring_view text, bool to_stderr) {
    DWORD const stream_id = to_stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
    HANDLE stream = GetStdHandle(stream_id);
    bool close_stream = false;
    if (stream == nullptr || stream == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) != 0 ||
            GetLastError() == ERROR_ACCESS_DENIED) {
            stream = GetStdHandle(stream_id);
        }
    }
    if (stream == nullptr || stream == INVALID_HANDLE_VALUE) {
        stream =
            CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stream != INVALID_HANDLE_VALUE) {
            close_stream = true;
        }
    }
    if (stream != nullptr && stream != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(stream, &mode) != 0) {
            DWORD written = 0;
            (void)WriteConsoleW(stream, text.data(), static_cast<DWORD>(text.size()),
                                &written, nullptr);
            if (close_stream) {
                CloseHandle(stream);
            }
            return;
        }
        if (!text.empty() && text.size() <= static_cast<size_t>(INT_MAX)) {
            int const source_chars = static_cast<int>(text.size());
            int const utf8_bytes = WideCharToMultiByte(
                CP_UTF8, 0, text.data(), source_chars, nullptr, 0, nullptr, nullptr);
            if (utf8_bytes > 0) {
                std::string utf8(static_cast<size_t>(utf8_bytes), '\0');
                int const converted =
                    WideCharToMultiByte(CP_UTF8, 0, text.data(), source_chars,
                                        utf8.data(), utf8_bytes, nullptr, nullptr);
                if (converted > 0) {
                    DWORD written = 0;
                    if (WriteFile(stream, utf8.data(), static_cast<DWORD>(converted),
                                  &written, nullptr) != 0) {
                        if (close_stream) {
                            CloseHandle(stream);
                        }
                        return;
                    }
                }
            }
        }
        if (close_stream) {
            CloseHandle(stream);
        }
    }
    std::wstring message(text);
    OutputDebugStringW(message.c_str());
}

void Write_console_line(std::wstring_view text, bool to_stderr) {
    Write_console_text(text, to_stderr);
    Write_console_text(L"\n", to_stderr);
}

void Write_console_block(std::wstring_view text, bool to_stderr) {
    if (text.empty()) {
        return;
    }
    size_t line_start = 0;
    while (line_start <= text.size()) {
        size_t const line_end = text.find(L'\n', line_start);
        if (line_end == std::wstring_view::npos) {
            Write_console_line(text.substr(line_start), to_stderr);
            break;
        }
        Write_console_line(text.substr(line_start, line_end - line_start), to_stderr);
        line_start = line_end + 1;
        if (line_start == text.size()) {
            break;
        }
    }
}

void Show_clipboard_result(greenflame::ClipboardCopyResult const &result,
                           greenflame::core::AppConfig const &config,
                           greenflame::TrayWindow &tray_window) {
    if (result.success) {
        if (config.show_balloons && !result.balloon_message.empty()) {
            tray_window.Show_balloon(greenflame::TrayBalloonIcon::Info,
                                     result.balloon_message.c_str(),
                                     Create_thumbnail_from_clipboard());
        }
        return;
    }
    if (!result.balloon_message.empty()) {
        tray_window.Show_balloon(greenflame::TrayBalloonIcon::Warning,
                                 result.balloon_message.c_str());
    }
}

struct ChangeNotificationGuard {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~ChangeNotificationGuard() {
        if (handle != INVALID_HANDLE_VALUE) {
            FindCloseChangeNotification(handle);
        }
    }
};

} // namespace

namespace greenflame {

GreenflameApp::GreenflameApp(HINSTANCE hinstance, core::CliOptions const &cli_options)
    : hinstance_(hinstance), tray_window_(this),
      overlay_window_(this, &config_, &window_query_),
      app_controller_(config_, display_queries_, window_inspector_, capture_service_,
                      file_system_service_),
      cli_options_(cli_options) {}

uint8_t GreenflameApp::Run() {
    Enable_per_monitor_dpi_awareness_v2();
    config_ = Load_app_config();
    if (core::Is_capture_mode(cli_options_.capture_mode)) {
        ProcessExitCode const cli_result = Run_cli_capture_mode();
        (void)Save_app_config(config_);
        return To_exit_code(cli_result);
    }

    if (!OverlayWindow::Register_window_class(hinstance_) ||
        !TrayWindow::Register_window_class(hinstance_)) {
        return To_exit_code(ProcessExitCode::WindowClassRegistrationFailed);
    }

    bool const testing_mode_enabled = Is_testing_mode_enabled(cli_options_);
    if (!tray_window_.Create(hinstance_, testing_mode_enabled)) {
        return To_exit_code(ProcessExitCode::TrayWindowCreateFailed);
    }

    ChangeNotificationGuard watcher;
    {
        std::filesystem::path const cfg_dir = Get_app_config_dir();
        if (!cfg_dir.empty()) {
            watcher.handle = FindFirstChangeNotificationW(
                cfg_dir.c_str(), FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
        }
    }

    constexpr DWORD k_debounce_ms = 400;
    bool debouncing = false;
    ULONGLONG debounce_deadline = 0;

    bool quit = false;
    MSG message{};
    while (!quit) {
        DWORD timeout = INFINITE;
        if (debouncing) {
            ULONGLONG const now = GetTickCount64();
            timeout = (now >= debounce_deadline)
                          ? 0
                          : static_cast<DWORD>(debounce_deadline - now);
        }

        DWORD const watch_count = (watcher.handle != INVALID_HANDLE_VALUE) ? 1u : 0u;
        DWORD const result = MsgWaitForMultipleObjects(
            watch_count, (watch_count > 0) ? &watcher.handle : nullptr, FALSE, timeout,
            QS_ALLINPUT);

        if (result == WAIT_OBJECT_0 && watch_count > 0) {
            FindNextChangeNotification(watcher.handle);
            debouncing = true;
            debounce_deadline = GetTickCount64() + k_debounce_ms;
        } else if (result == WAIT_TIMEOUT) {
            debouncing = false;
            config_ = Load_app_config();
            continue;
        }

        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    (void)Save_app_config(config_);
    return static_cast<uint8_t>(message.wParam);
}

ProcessExitCode GreenflameApp::Run_cli_capture_mode() {
    CliResult const cli_result = app_controller_.Run_cli_capture_mode(cli_options_);
    if (!cli_result.stderr_message.empty()) {
        Write_console_block(cli_result.stderr_message, true);
    }
    if (!cli_result.stdout_message.empty()) {
        Write_console_block(cli_result.stdout_message, false);
    }
    return cli_result.exit_code;
}

void GreenflameApp::On_start_capture_requested() {
    (void)overlay_window_.Create_and_show(hinstance_);
}

void GreenflameApp::On_copy_window_to_clipboard_requested(HWND target_window) {
    if (overlay_window_.Is_open()) {
        return;
    }
    ClipboardCopyResult const result =
        app_controller_.On_copy_window_to_clipboard_requested(target_window);
    Show_clipboard_result(result, config_, tray_window_);
}

void GreenflameApp::On_copy_monitor_to_clipboard_requested() {
    if (overlay_window_.Is_open()) {
        return;
    }
    ClipboardCopyResult const result =
        app_controller_.On_copy_monitor_to_clipboard_requested();
    Show_clipboard_result(result, config_, tray_window_);
}

void GreenflameApp::On_copy_desktop_to_clipboard_requested() {
    if (overlay_window_.Is_open()) {
        return;
    }
    ClipboardCopyResult const result =
        app_controller_.On_copy_desktop_to_clipboard_requested();
    Show_clipboard_result(result, config_, tray_window_);
}

void GreenflameApp::On_copy_last_region_to_clipboard_requested() {
    if (overlay_window_.Is_open()) {
        return;
    }
    ClipboardCopyResult const result =
        app_controller_.On_copy_last_region_to_clipboard_requested();
    Show_clipboard_result(result, config_, tray_window_);
}

void GreenflameApp::On_copy_last_window_to_clipboard_requested() {
    if (overlay_window_.Is_open()) {
        return;
    }
    ClipboardCopyResult const result =
        app_controller_.On_copy_last_window_to_clipboard_requested();
    Show_clipboard_result(result, config_, tray_window_);
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
    ClipboardCopyResult const result =
        app_controller_.On_selection_copied_to_clipboard(screen_rect, window);
    if (config_.show_balloons && result.success && !result.balloon_message.empty()) {
        tray_window_.Show_balloon(TrayBalloonIcon::Info, result.balloon_message.c_str(),
                                  Create_thumbnail_from_clipboard());
    }
}

void GreenflameApp::On_selection_saved_to_file(core::RectPx screen_rect,
                                               std::optional<HWND> window,
                                               HBITMAP thumbnail,
                                               std::wstring_view saved_path,
                                               bool file_copied_to_clipboard) {
    SelectionSavedResult const result = app_controller_.On_selection_saved_to_file(
        screen_rect, window, saved_path, file_copied_to_clipboard);
    if (config_.show_balloons) {
        tray_window_.Show_balloon(TrayBalloonIcon::Info, result.balloon_message.c_str(),
                                  thumbnail);
    } else if (thumbnail != nullptr) {
        DeleteObject(thumbnail);
    }
}

} // namespace greenflame
