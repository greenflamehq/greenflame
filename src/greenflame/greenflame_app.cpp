#include "greenflame_app.h"

#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/save_image_policy.h"
#include "win/display_queries.h"
#include "win/gdi_capture.h"
#include "win/save_image.h"
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
constexpr int kCliExitMissingRegion = 3;
constexpr int kCliExitWindowNotFound = 4;
constexpr int kCliExitWindowAmbiguous = 5;
constexpr int kCliExitNoMonitors = 6;
constexpr int kCliExitMonitorOutOfRange = 7;
constexpr int kCliExitOutputPathFailure = 9;
constexpr int kCliExitCaptureSaveFailed = 10;
constexpr int kCliExitWindowUnavailable = 11;
constexpr int kCliExitWindowMinimized = 12;

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

[[nodiscard]] std::wstring
Resolve_save_directory_from_config(greenflame::AppConfig const &config) {
    std::wstring dir;
    if (!config.default_save_dir.empty()) {
        dir = config.default_save_dir;
    } else {
        wchar_t pictures_dir[MAX_PATH] = {};
        SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, pictures_dir);
        dir = pictures_dir;
        dir += L"\\greenflame";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

[[nodiscard]] std::vector<std::wstring>
List_directory_filenames(std::wstring_view dir) {
    std::vector<std::wstring> result;
    std::wstring search_path(dir);
    if (!search_path.empty() && search_path.back() != L'\\') {
        search_path += L'\\';
    }
    search_path += L'*';
    WIN32_FIND_DATAW fd{};
    HANDLE const handle = FindFirstFileW(search_path.c_str(), &fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return result;
    }
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            result.emplace_back(fd.cFileName);
        }
    } while (FindNextFileW(handle, &fd));
    FindClose(handle);
    return result;
}

[[nodiscard]] std::wstring_view
Pattern_for_source(greenflame::AppConfig const &config,
                   greenflame::core::SaveSelectionSource source) {
    switch (source) {
    case greenflame::core::SaveSelectionSource::Region:
        return config.filename_pattern_region;
    case greenflame::core::SaveSelectionSource::Window:
        return config.filename_pattern_window;
    case greenflame::core::SaveSelectionSource::Monitor:
        return config.filename_pattern_monitor;
    case greenflame::core::SaveSelectionSource::Desktop:
        return config.filename_pattern_desktop;
    }
    return {};
}

[[nodiscard]] uint32_t
Default_filter_index_from_config(greenflame::AppConfig const &config) noexcept {
    if (config.default_save_format == L"jpg") {
        return 2;
    }
    if (config.default_save_format == L"bmp") {
        return 3;
    }
    return 1;
}

[[nodiscard]] std::wstring
Default_extension_from_config(greenflame::AppConfig const &config) {
    uint32_t const filter_index = Default_filter_index_from_config(config);
    if (filter_index == 2) {
        return L".jpg";
    }
    if (filter_index == 3) {
        return L".bmp";
    }
    return L".png";
}

[[nodiscard]] std::wstring Build_default_output_path(
    greenflame::AppConfig const &config, greenflame::core::SaveSelectionSource source,
    std::optional<size_t> monitor_index_zero_based, std::wstring_view window_title) {
    std::wstring const save_dir = Resolve_save_directory_from_config(config);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    greenflame::core::FilenamePatternContext context{};
    context.timestamp.day = st.wDay;
    context.timestamp.month = st.wMonth;
    context.timestamp.year = st.wYear;
    context.timestamp.hour = st.wHour;
    context.timestamp.minute = st.wMinute;
    context.timestamp.second = st.wSecond;
    context.monitor_index_zero_based = monitor_index_zero_based;
    context.window_title = window_title;

    std::wstring_view const configured_pattern = Pattern_for_source(config, source);
    std::wstring_view const effective_pattern =
        configured_pattern.empty() ? greenflame::core::Default_filename_pattern(source)
                                   : configured_pattern;
    if (greenflame::core::Pattern_uses_num(effective_pattern)) {
        std::vector<std::wstring> const files = List_directory_filenames(save_dir);
        context.incrementing_number = greenflame::core::Find_next_num_for_pattern(
            effective_pattern, context, files);
    }

    std::wstring const base_name =
        greenflame::core::Build_default_save_name(source, context, configured_pattern);
    std::wstring output_path = save_dir;
    if (!output_path.empty() && output_path.back() != L'\\') {
        output_path += L'\\';
    }
    output_path += base_name;
    output_path += Default_extension_from_config(config);
    return output_path;
}

[[nodiscard]] std::wstring
Resolve_output_path(greenflame::AppConfig const &config,
                    greenflame::core::SaveSelectionSource source,
                    std::optional<size_t> monitor_index_zero_based,
                    std::wstring_view window_title, std::wstring_view explicit_path) {
    if (!explicit_path.empty()) {
        return greenflame::core::Ensure_image_save_extension(
            explicit_path, Default_filter_index_from_config(config));
    }
    return Build_default_output_path(config, source, monitor_index_zero_based,
                                     window_title);
}

void Update_default_save_dir_from_path(greenflame::AppConfig &config,
                                       std::wstring_view full_path) {
    size_t const slash = full_path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return;
    }
    config.default_save_dir = std::wstring(full_path.substr(0, slash));
    config.Normalize();
}

enum class SaveScreenRectFailure : uint8_t {
    None = 0,
    EmptyRect = 1,
    EmptyOutputPath = 2,
    OutsideVirtualDesktop = 3,
    CaptureFailed = 4,
    CropFailed = 5,
    EncodeFailed = 6,
};

struct SaveScreenRectResult {
    bool ok = false;
    SaveScreenRectFailure failure = SaveScreenRectFailure::None;
};

[[nodiscard]] SaveScreenRectResult
Save_screen_rect_to_file(greenflame::core::RectPx screen_rect,
                         std::wstring_view output_path) {
    if (screen_rect.Is_empty() || output_path.empty()) {
        return SaveScreenRectResult{
            false, screen_rect.Is_empty() ? SaveScreenRectFailure::EmptyRect
                                          : SaveScreenRectFailure::EmptyOutputPath};
    }

    greenflame::core::RectPx const virtual_bounds =
        greenflame::Get_virtual_desktop_bounds_px();
    std::optional<greenflame::core::RectPx> const clipped_screen =
        greenflame::core::RectPx::Clip(screen_rect, virtual_bounds);
    if (!clipped_screen.has_value()) {
        return SaveScreenRectResult{false,
                                    SaveScreenRectFailure::OutsideVirtualDesktop};
    }

    greenflame::GdiCaptureResult capture{};
    if (!greenflame::Capture_virtual_desktop(capture)) {
        return SaveScreenRectResult{false, SaveScreenRectFailure::CaptureFailed};
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
        return SaveScreenRectResult{false, SaveScreenRectFailure::CropFailed};
    }

    std::wstring const output_path_string(output_path);
    bool saved = false;
    greenflame::core::ImageSaveFormat const format =
        greenflame::core::Detect_image_save_format_from_path(output_path_string);
    if (format == greenflame::core::ImageSaveFormat::Jpeg) {
        saved = greenflame::Save_capture_to_jpeg(cropped, output_path_string.c_str());
    } else if (format == greenflame::core::ImageSaveFormat::Bmp) {
        saved = greenflame::Save_capture_to_bmp(cropped, output_path_string.c_str());
    } else {
        saved = greenflame::Save_capture_to_png(cropped, output_path_string.c_str());
    }
    cropped.Free();
    if (!saved) {
        return SaveScreenRectResult{false, SaveScreenRectFailure::EncodeFailed};
    }
    return SaveScreenRectResult{true, SaveScreenRectFailure::None};
}

[[nodiscard]] bool Contains_no_case(std::wstring_view text,
                                    std::wstring_view needle) noexcept {
    if (needle.empty()) {
        return false;
    }
    if (needle.size() > text.size()) {
        return false;
    }
    for (size_t start = 0; start + needle.size() <= text.size(); ++start) {
        bool match = true;
        for (size_t i = 0; i < needle.size(); ++i) {
            wchar_t const a = static_cast<wchar_t>(std::towlower(text[start + i]));
            wchar_t const b = static_cast<wchar_t>(std::towlower(needle[i]));
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool Equals_no_case(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t const x = static_cast<wchar_t>(std::towlower(a[i]));
        wchar_t const y = static_cast<wchar_t>(std::towlower(b[i]));
        if (x != y) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool Is_terminal_window_class(std::wstring_view class_name) noexcept {
    return Equals_no_case(class_name, L"ConsoleWindowClass") ||
           Equals_no_case(class_name, L"CASCADIA_HOSTING_WINDOW_CLASS");
}

struct WindowTitleMatch {
    HWND hwnd = nullptr;
    greenflame::core::RectPx rect = {};
    std::wstring title = {};
    std::wstring class_name = {};
};

struct WindowSearchState {
    std::wstring_view needle = {};
    std::vector<WindowTitleMatch> matches = {};
};

BOOL CALLBACK Enum_windows_by_title_proc(HWND hwnd, LPARAM lparam) {
    auto *state = reinterpret_cast<WindowSearchState *>(lparam);
    if (state == nullptr) {
        return FALSE;
    }
    if (!IsWindowVisible(hwnd) || GetParent(hwnd) != nullptr) {
        return TRUE;
    }
    int const title_len = GetWindowTextLengthW(hwnd);
    if (title_len <= 0) {
        return TRUE;
    }
    std::wstring title(static_cast<size_t>(title_len) + 1, L'\0');
    int const copied =
        GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
    if (copied <= 0) {
        return TRUE;
    }
    title.resize(static_cast<size_t>(copied));
    if (!Contains_no_case(title, state->needle)) {
        return TRUE;
    }
    wchar_t class_name_buf[256] = {};
    int const class_name_len = GetClassNameW(
        hwnd, class_name_buf, static_cast<int>(std::size(class_name_buf)));
    std::wstring class_name;
    if (class_name_len > 0) {
        class_name.assign(class_name_buf, static_cast<size_t>(class_name_len));
    }
    std::optional<greenflame::core::RectPx> const rect =
        greenflame::Get_window_rect(hwnd);
    if (!rect.has_value()) {
        return TRUE;
    }
    state->matches.push_back(WindowTitleMatch{hwnd, *rect, title, class_name});
    return TRUE;
}

[[nodiscard]] std::vector<WindowTitleMatch>
Find_windows_by_title_contains(std::wstring_view needle) {
    WindowSearchState state{};
    state.needle = needle;
    EnumWindows(Enum_windows_by_title_proc, reinterpret_cast<LPARAM>(&state));
    return state.matches;
}

[[nodiscard]] bool Is_cli_invocation_window(WindowTitleMatch const &match,
                                            std::wstring_view query) noexcept {
    if (!Contains_no_case(match.title, L"greenflame.exe")) {
        return false;
    }
    if (!Contains_no_case(match.title, query)) {
        return false;
    }

    // Typical terminal case: class is a known console/terminal window class.
    if (Is_terminal_window_class(match.class_name)) {
        return true;
    }

    // Fallback: any window title that visibly includes our `-w/--window`
    // invocation command line is treated as the invocation shell window.
    return Contains_no_case(match.title, L"--window") ||
           Contains_no_case(match.title, L"-w ");
}

[[nodiscard]] std::vector<WindowTitleMatch>
Filter_cli_invocation_window(std::vector<WindowTitleMatch> const &matches,
                             std::wstring_view query) {
    std::vector<WindowTitleMatch> filtered;
    filtered.reserve(matches.size());
    for (WindowTitleMatch const &match : matches) {
        if (Is_cli_invocation_window(match, query)) {
            continue;
        }
        filtered.push_back(match);
    }
    return filtered;
}

[[nodiscard]] std::wstring Format_window_candidate_line(WindowTitleMatch const &match,
                                                        size_t index) {
    greenflame::core::RectPx const normalized = match.rect.Normalized();
    wchar_t hwnd_buffer[32] = {};
    swprintf_s(hwnd_buffer, L"%p", static_cast<void *>(match.hwnd));

    std::wstring line = L"  [";
    line += std::to_wstring(index + 1);
    line += L"] \"";
    line += match.title;
    line += L"\" (hwnd=";
    line += hwnd_buffer;
    line += L", x=";
    line += std::to_wstring(normalized.left);
    line += L", y=";
    line += std::to_wstring(normalized.top);
    line += L", w=";
    line += std::to_wstring(normalized.Width());
    line += L", h=";
    line += std::to_wstring(normalized.Height());
    line += L")";
    return line;
}

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

GreenflameApp::GreenflameApp(HINSTANCE hinstance, core::CliOptions const &cli_options)
    : hinstance_(hinstance), cli_options_(cli_options), tray_window_(this),
      overlay_window_(this, &config_) {}

int GreenflameApp::Run() {
    Enable_per_monitor_dpi_awareness_v2();
    config_ = AppConfig::Load();
    if (core::Is_capture_mode(cli_options_.capture_mode)) {
        int const cli_result = Run_cli_capture_mode();
        (void)config_.Save();
        return cli_result;
    }

    if (!OverlayWindow::Register_window_class(hinstance_) ||
        !TrayWindow::Register_window_class(hinstance_)) {
        return 1;
    }

    bool const testing_mode_enabled = Is_testing_mode_enabled(cli_options_);
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

int GreenflameApp::Run_cli_capture_mode() {
    core::RectPx target_rect = {};
    core::SaveSelectionSource source = core::SaveSelectionSource::Region;
    std::optional<size_t> monitor_index_zero_based = std::nullopt;
    std::wstring window_title = {};
    std::optional<HWND> captured_window = std::nullopt;
    WindowObscuration window_obscuration = WindowObscuration::None;
    bool window_partially_out_of_bounds = false;

    switch (cli_options_.capture_mode) {
    case core::CliCaptureMode::Region:
        if (!cli_options_.region_px.has_value()) {
            Write_console_line(L"Error: --region is required.", true);
            return kCliExitMissingRegion;
        }
        target_rect = *cli_options_.region_px;
        source = core::SaveSelectionSource::Region;
        break;
    case core::CliCaptureMode::Window: {
        std::vector<WindowTitleMatch> const raw_matches =
            Find_windows_by_title_contains(cli_options_.window_name);
        std::vector<WindowTitleMatch> const matches =
            Filter_cli_invocation_window(raw_matches, cli_options_.window_name);
        if (matches.empty()) {
            std::wstring message = L"Error: No visible window matches: ";
            message += cli_options_.window_name;
            Write_console_line(message, true);
            return kCliExitWindowNotFound;
        }
        if (matches.size() > 1) {
            std::wstring message = L"Error: Window name is ambiguous (";
            message += std::to_wstring(matches.size());
            message += L" matches): ";
            message += cli_options_.window_name;
            Write_console_line(message, true);
            Write_console_line(L"Matching windows:", true);
            for (size_t i = 0; i < matches.size(); ++i) {
                Write_console_line(Format_window_candidate_line(matches[i], i), true);
            }
            return kCliExitWindowAmbiguous;
        }
        captured_window = matches.front().hwnd;
        if (IsWindow(*captured_window) == 0) {
            Write_console_line(
                L"Error: Matched window is no longer available. Try again.", true);
            return kCliExitWindowUnavailable;
        }
        if (IsIconic(*captured_window) != 0) {
            Write_console_line(
                L"Error: Matched window is minimized. Restore it and try again.", true);
            return kCliExitWindowMinimized;
        }
        std::optional<core::RectPx> const current_rect =
            Get_window_rect(*captured_window);
        if (!current_rect.has_value()) {
            Write_console_line(
                L"Error: Matched window is no longer capturable. Try again.", true);
            return kCliExitWindowUnavailable;
        }
        target_rect = *current_rect;
        window_title = matches.front().title;
        source = core::SaveSelectionSource::Window;
        window_obscuration = Get_window_obscuration(*captured_window);
        core::RectPx const virtual_bounds = Get_virtual_desktop_bounds_px();
        std::optional<core::RectPx> const clipped_to_virtual =
            core::RectPx::Clip(target_rect, virtual_bounds);
        window_partially_out_of_bounds =
            clipped_to_virtual.has_value() && *clipped_to_virtual != target_rect;
        break;
    }
    case core::CliCaptureMode::Monitor: {
        std::vector<core::MonitorWithBounds> const monitors =
            Get_monitors_with_bounds();
        if (monitors.empty()) {
            Write_console_line(L"Error: No monitors are available.", true);
            return kCliExitNoMonitors;
        }
        if (cli_options_.monitor_id < 1 ||
            static_cast<size_t>(cli_options_.monitor_id) > monitors.size()) {
            std::wstring message = L"Error: --monitor id is out of range (1..";
            message += std::to_wstring(monitors.size());
            message += L").";
            Write_console_line(message, true);
            return kCliExitMonitorOutOfRange;
        }
        monitor_index_zero_based = static_cast<size_t>(cli_options_.monitor_id - 1);
        target_rect = monitors[*monitor_index_zero_based].bounds;
        source = core::SaveSelectionSource::Monitor;
        break;
    }
    case core::CliCaptureMode::Desktop:
        target_rect = Get_virtual_desktop_bounds_px();
        source = core::SaveSelectionSource::Desktop;
        break;
    case core::CliCaptureMode::Help:
    case core::CliCaptureMode::None:
        return 0;
    }

    bool const has_explicit_output_path = !cli_options_.output_path.empty();
    std::wstring output_path =
        Resolve_output_path(config_, source, monitor_index_zero_based, window_title,
                            cli_options_.output_path);
    if (output_path.empty()) {
        Write_console_line(L"Error: Unable to resolve output path.", true);
        return kCliExitOutputPathFailure;
    }
    bool reserved_output_path = false;
    if (!has_explicit_output_path) {
        std::wstring const reserved = Reserve_unique_file_path(output_path);
        if (reserved.empty()) {
            Write_console_line(L"Error: Unable to reserve an output path.", true);
            return kCliExitOutputPathFailure;
        }
        output_path = reserved;
        reserved_output_path = true;
    }

    if (window_obscuration == WindowObscuration::Full) {
        Write_console_line(
            L"Warning: Matched window is fully obscured by other windows. The "
            L"saved image may not show that window.",
            true);
    } else if (window_obscuration == WindowObscuration::Partial &&
               window_partially_out_of_bounds) {
        Write_console_line(
            L"Warning: Matched window is partially obscured and partially outside "
            L"visible desktop bounds. The saved image may include other windows "
            L"and may clip the target window.",
            true);
    } else if (window_obscuration == WindowObscuration::Partial) {
        Write_console_line(
            L"Warning: Matched window is partially obscured by other windows. The "
            L"saved image may include those windows.",
            true);
    } else if (window_partially_out_of_bounds) {
        Write_console_line(
            L"Warning: Matched window is partially outside visible desktop "
            L"bounds. The saved image may clip the target window.",
            true);
    }

    SaveScreenRectResult const save_result =
        Save_screen_rect_to_file(target_rect, output_path);
    if (!save_result.ok) {
        if (reserved_output_path) {
            (void)DeleteFileW(output_path.c_str());
        }
        switch (save_result.failure) {
        case SaveScreenRectFailure::OutsideVirtualDesktop:
            if (cli_options_.capture_mode == core::CliCaptureMode::Window) {
                Write_console_line(
                    L"Error: Matched window is completely outside the virtual "
                    L"desktop. Nothing to capture.",
                    true);
            } else {
                Write_console_line(
                    L"Error: Requested capture area is outside the virtual desktop.",
                    true);
            }
            break;
        case SaveScreenRectFailure::CaptureFailed:
            Write_console_line(L"Error: Failed to capture the virtual desktop image.",
                               true);
            break;
        case SaveScreenRectFailure::CropFailed:
            Write_console_line(L"Error: Failed to crop the captured image.", true);
            break;
        case SaveScreenRectFailure::EncodeFailed: {
            std::wstring message = L"Error: Failed to encode or write image file: ";
            message += output_path;
            Write_console_line(message, true);
            break;
        }
        case SaveScreenRectFailure::EmptyRect:
        case SaveScreenRectFailure::EmptyOutputPath:
        case SaveScreenRectFailure::None:
            Write_console_line(L"Error: Internal capture request was invalid.", true);
            break;
        }
        return kCliExitCaptureSaveFailed;
    }

    Update_default_save_dir_from_path(config_, output_path);
    Store_last_capture(target_rect, captured_window);
    config_.Normalize();

    std::wstring message = L"Saved: ";
    message += output_path;
    Write_console_line(message, false);
    return 0;
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
        tray_window_.Show_balloon(TrayBalloonIcon::Info, kClipboardCopiedBalloonMessage,
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
    core::RectPx const normalized = screen_rect.Normalized();
    last_capture_screen_rect_ = normalized;
    if (window.has_value()) {
        last_capture_window_ = *window;
    }
}

} // namespace greenflame
