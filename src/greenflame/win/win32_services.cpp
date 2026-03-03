#include "win/win32_services.h"

#include "greenflame_core/string_utils.h"
#include "win/display_queries.h"
#include "win/gdi_capture.h"
#include "win/save_image.h"

namespace {

struct WindowSearchState {
    std::wstring_view needle = {};
    std::vector<greenflame::WindowMatch> matches = {};
    greenflame::IWindowQuery const *window_query = nullptr;
    bool had_exception = false;
};

BOOL CALLBACK Enum_windows_by_title_proc(HWND hwnd, LPARAM lparam) noexcept {
    auto *state = reinterpret_cast<WindowSearchState *>(lparam);
    if (state == nullptr) {
        return FALSE;
    }

    try {
        if (IsWindowVisible(hwnd) == 0 || GetParent(hwnd) != nullptr) {
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

        if (!greenflame::core::Contains_no_case(title, state->needle)) {
            return TRUE;
        }

        wchar_t class_name_buffer[256] = {};
        int const class_name_len = GetClassNameW(hwnd, class_name_buffer, 256);
        std::wstring class_name = {};
        if (class_name_len > 0) {
            class_name.assign(class_name_buffer, static_cast<size_t>(class_name_len));
        }

        if (state->window_query == nullptr) {
            return FALSE;
        }

        std::optional<greenflame::core::RectPx> const rect =
            state->window_query->Get_window_rect(hwnd);
        if (!rect.has_value()) {
            return TRUE;
        }

        greenflame::core::WindowCandidateInfo info{};
        info.title = title;
        info.class_name = class_name;
        info.rect = *rect;

        state->matches.push_back(greenflame::WindowMatch{info, hwnd});
        return TRUE;
    } catch (...) {
        state->had_exception = true;
        return FALSE;
    }
}

[[nodiscard]] greenflame::core::RectPx
Capture_rect_from_screen_rect(greenflame::core::RectPx screen_rect,
                              greenflame::core::RectPx virtual_bounds) {
    return greenflame::core::RectPx::From_ltrb(screen_rect.left - virtual_bounds.left,
                                               screen_rect.top - virtual_bounds.top,
                                               screen_rect.right - virtual_bounds.left,
                                               screen_rect.bottom - virtual_bounds.top);
}

} // namespace

namespace greenflame {

core::PointPx Win32DisplayQueries::Get_cursor_pos_px() const {
    return greenflame::Get_cursor_pos_px();
}

core::RectPx Win32DisplayQueries::Get_virtual_desktop_bounds_px() const {
    return greenflame::Get_virtual_desktop_bounds_px();
}

std::vector<core::MonitorWithBounds>
Win32DisplayQueries::Get_monitors_with_bounds() const {
    return greenflame::Get_monitors_with_bounds();
}

std::optional<core::RectPx> Win32WindowInspector::Get_window_rect(HWND hwnd) const {
    return window_query_.Get_window_rect(hwnd);
}

bool Win32WindowInspector::Is_window_valid(HWND hwnd) const {
    return IsWindow(hwnd) != 0;
}

bool Win32WindowInspector::Is_window_minimized(HWND hwnd) const {
    return IsIconic(hwnd) != 0;
}

WindowObscuration Win32WindowInspector::Get_window_obscuration(HWND hwnd) const {
    return window_query_.Get_window_obscuration(hwnd);
}

std::optional<core::RectPx>
Win32WindowInspector::Get_foreground_window_rect(HWND exclude_hwnd) const {
    return window_query_.Get_foreground_window_rect(exclude_hwnd);
}

std::optional<core::RectPx>
Win32WindowInspector::Get_window_rect_under_cursor(POINT screen_pt,
                                                   HWND exclude_hwnd) const {
    return window_query_.Get_window_rect_under_cursor(screen_pt, exclude_hwnd);
}

std::vector<WindowMatch>
Win32WindowInspector::Find_windows_by_title(std::wstring_view needle) const {
    WindowSearchState state{};
    state.needle = needle;
    state.window_query = &window_query_;
    (void)EnumWindows(Enum_windows_by_title_proc, reinterpret_cast<LPARAM>(&state));
    if (state.had_exception) {
        return {};
    }
    return state.matches;
}

bool Win32CaptureService::Copy_rect_to_clipboard(core::RectPx screen_rect) {
    if (screen_rect.Is_empty()) {
        return false;
    }

    core::RectPx const virtual_bounds = greenflame::Get_virtual_desktop_bounds_px();
    std::optional<core::RectPx> const clipped_screen =
        core::RectPx::Clip(screen_rect, virtual_bounds);
    if (!clipped_screen.has_value()) {
        return false;
    }

    GdiCaptureResult capture{};
    if (!greenflame::Capture_virtual_desktop(capture)) {
        return false;
    }

    core::RectPx const capture_rect =
        Capture_rect_from_screen_rect(*clipped_screen, virtual_bounds);
    GdiCaptureResult cropped{};
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

bool Win32CaptureService::Save_rect_to_file(core::RectPx screen_rect,
                                            std::wstring_view path,
                                            core::ImageSaveFormat format) {
    if (screen_rect.Is_empty() || path.empty()) {
        return false;
    }

    core::RectPx const virtual_bounds = greenflame::Get_virtual_desktop_bounds_px();
    std::optional<core::RectPx> const clipped_screen =
        core::RectPx::Clip(screen_rect, virtual_bounds);
    if (!clipped_screen.has_value()) {
        return false;
    }

    GdiCaptureResult capture{};
    if (!greenflame::Capture_virtual_desktop(capture)) {
        return false;
    }

    core::RectPx const capture_rect =
        Capture_rect_from_screen_rect(*clipped_screen, virtual_bounds);
    GdiCaptureResult cropped{};
    bool const cropped_ok =
        greenflame::Crop_capture(capture, capture_rect.left, capture_rect.top,
                                 capture_rect.Width(), capture_rect.Height(), cropped);
    capture.Free();
    if (!cropped_ok) {
        return false;
    }

    std::wstring const output_path(path);
    bool const saved =
        greenflame::Save_capture_to_file(cropped, output_path.c_str(), format);
    cropped.Free();
    return saved;
}

std::vector<std::wstring>
Win32FileSystemService::List_directory_filenames(std::wstring_view dir) const {
    return greenflame::List_directory_filenames(dir);
}

std::wstring
Win32FileSystemService::Reserve_unique_file_path(std::wstring_view desired) const {
    return greenflame::Reserve_unique_file_path(desired);
}

bool Win32FileSystemService::Try_reserve_exact_file_path(std::wstring_view path,
                                                         bool &already_exists) const {
    already_exists = false;
    if (path.empty()) {
        return false;
    }

    std::wstring const path_string(path);
    HANDLE const handle =
        CreateFileW(path_string.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        return true;
    }

    DWORD const error = GetLastError();
    already_exists = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
    return false;
}

std::wstring Win32FileSystemService::Resolve_save_directory(
    std::wstring const &configured_dir) const {
    std::wstring dir = configured_dir;
    if (dir.empty()) {
        wchar_t pictures_dir[MAX_PATH] = {};
        SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, pictures_dir);
        dir = pictures_dir;
        dir += L"\\greenflame";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring
Win32FileSystemService::Resolve_absolute_path(std::wstring_view path) const {
    if (path.empty()) {
        return {};
    }

    std::wstring input(path);
    DWORD const required = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (required == 0) {
        return input;
    }

    std::wstring result;
    result.resize(required);
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

void Win32FileSystemService::Delete_file_if_exists(std::wstring_view path) const {
    if (path.empty()) {
        return;
    }
    std::wstring const path_string(path);
    (void)DeleteFileW(path_string.c_str());
}

core::SaveTimestamp Win32FileSystemService::Get_current_timestamp() const {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    core::SaveTimestamp timestamp{};
    timestamp.day = st.wDay;
    timestamp.month = st.wMonth;
    timestamp.year = st.wYear;
    timestamp.hour = st.wHour;
    timestamp.minute = st.wMinute;
    timestamp.second = st.wSecond;
    return timestamp;
}

} // namespace greenflame
