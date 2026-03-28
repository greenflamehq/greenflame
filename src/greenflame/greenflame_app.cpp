#include "greenflame_app.h"
#include "app_config_store.h"
#include "console_output.h"
#include "win/gdi_capture.h"
#include "win/startup_launch.h"

namespace {

constexpr int kThumbnailMaxWidth = 320;
constexpr int kThumbnailMaxHeight = 120;
constexpr wchar_t kStartupToggleFailedMessage[] =
    L"Failed to update 'Start with Windows' setting.";
constexpr wchar_t kIncludeCursorToggleFailedMessage[] =
    L"Failed to update 'Include captured cursor' setting.";

[[nodiscard]] HBITMAP Create_thumbnail_from_clipboard() {
    if (OpenClipboard(nullptr) == 0) {
        return nullptr;
    }
    HBITMAP const clip_bmp = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    HBITMAP result = nullptr;
    if (clip_bmp != nullptr) {
        BITMAP bm{};
        if (GetObject(clip_bmp, sizeof(bm), &bm) != 0 && bm.bmWidth > 0 &&
            bm.bmHeight > 0) {
            result = greenflame::Scale_bitmap_to_thumbnail(
                clip_bmp, bm.bmWidth, bm.bmHeight, kThumbnailMaxWidth,
                kThumbnailMaxHeight);
        }
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

[[nodiscard]] std::wstring First_sentence(std::wstring_view text) {
    size_t const sentence_end = text.find(L'.');
    if (sentence_end == std::wstring_view::npos) {
        return std::wstring(text);
    }

    return std::wstring(text.substr(0, sentence_end + 1));
}

[[nodiscard]] std::wstring
Build_config_issue_stderr_text(greenflame::AppConfigLoadResult const &load_result) {
    if (!load_result.issue.has_value()) {
        return {};
    }

    std::wstring text = L"Config file: ";
    text += load_result.path.wstring();
    text += L"\n";
    text += load_result.issue->summary;
    if (!load_result.issue->detail.empty()) {
        text += L"\n";
        text += load_result.issue->detail;
    }
    if (!load_result.issue->consequences.empty()) {
        text += L"\n";
        text += First_sentence(load_result.issue->consequences);
    }
    return text;
}

void Show_config_issue(greenflame::AppConfigLoadResult const &load_result,
                       greenflame::TrayWindow &tray_window) {
    if (!load_result.issue.has_value()) {
        return;
    }

    tray_window.Show_balloon(
        greenflame::TrayBalloonIcon::Error, load_result.issue->summary.c_str(), nullptr,
        load_result.path.wstring(), greenflame::ToastFileAction::OpenFile,
        load_result.issue->detail.empty() ? nullptr : load_result.issue->detail.c_str(),
        load_result.issue->consequences.empty()
            ? nullptr
            : load_result.issue->consequences.c_str());
}

void Show_config_recovery(greenflame::TrayWindow &tray_window) {
    tray_window.Show_balloon(
        greenflame::TrayBalloonIcon::Info,
        L"Config file restored. Persistent changes will be saved again.");
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
                      input_image_service_, annotation_preparation_service_,
                      file_system_service_),
      cli_options_(cli_options) {}

uint8_t GreenflameApp::Run() {
    Enable_per_monitor_dpi_awareness_v2();
    AppConfigLoadResult load_result = Load_app_config();
    config_ = load_result.config;
    overlay_help_content_ = app_controller_.Build_overlay_help_content();
    overlay_window_.Set_hotkey_help_content(&overlay_help_content_);
    if (core::Has_cli_render_source(cli_options_)) {
        if (load_result.issue.has_value()) {
            Write_console_block(Build_config_issue_stderr_text(load_result), true);
        }
        ProcessExitCode const cli_result = Run_cli_capture_mode();
        (void)Save_app_config(config_);
        return To_exit_code(cli_result);
    }

    if (!OverlayWindow::Register_window_class(hinstance_) ||
        !TrayWindow::Register_window_class(hinstance_)) {
        return To_exit_code(ProcessExitCode::WindowClassRegistrationFailed);
    }

    bool const testing_mode_enabled = Is_testing_mode_enabled(cli_options_);
    overlay_window_.Set_testing_toolbar(testing_mode_enabled);
    bool const start_with_windows_enabled = Is_startup_launch_enabled();
    if (!tray_window_.Create(hinstance_, testing_mode_enabled,
                             start_with_windows_enabled)) {
        return To_exit_code(ProcessExitCode::TrayWindowCreateFailed);
    }
    Show_config_issue(load_result, tray_window_);

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
            bool const had_issue = load_result.issue.has_value();
            load_result = Load_app_config();
            config_ = load_result.config;
            if (had_issue && !load_result.issue.has_value()) {
                Show_config_recovery(tray_window_);
            }
            if (!had_issue && load_result.issue.has_value()) {
                Show_config_issue(load_result, tray_window_);
            }
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

bool GreenflameApp::Is_include_cursor_enabled() const { return config_.include_cursor; }

bool GreenflameApp::On_set_include_cursor_enabled(bool enabled) {
    bool const previous_value = config_.include_cursor;
    config_.include_cursor = enabled;
    config_.Normalize();
    if (Save_app_config(config_)) {
        return true;
    }

    config_.include_cursor = previous_value;
    config_.Normalize();
    tray_window_.Show_balloon(TrayBalloonIcon::Warning,
                              kIncludeCursorToggleFailedMessage);
    return false;
}

bool GreenflameApp::On_set_start_with_windows_enabled(bool enabled) {
    bool const updated = Set_startup_launch_enabled(enabled);
    if (!updated) {
        tray_window_.Show_balloon(TrayBalloonIcon::Warning,
                                  kStartupToggleFailedMessage);
    }
    return updated;
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
                                  thumbnail, result.file_path);
    } else if (thumbnail != nullptr) {
        DeleteObject(thumbnail);
    }
}

} // namespace greenflame
