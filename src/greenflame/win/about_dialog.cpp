#include "win/about_dialog.h"

namespace {

constexpr int kAppIconResourceId = 1;
constexpr int kAboutDialogResourceId = 101;
constexpr int kAboutDialogIconControlId = 1001;
constexpr int kAboutDialogIconSize = 64;

void Center_dialog_on_cursor_monitor(HWND hwnd) {
    RECT dialog_rect{};
    if (GetWindowRect(hwnd, &dialog_rect) == 0) {
        return;
    }

    POINT cursor{};
    if (GetCursorPos(&cursor) == 0) {
        return;
    }
    HMONITOR const monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (monitor == nullptr || GetMonitorInfoW(monitor, &monitor_info) == 0) {
        return;
    }

    RECT const monitor_rect = monitor_info.rcMonitor;
    int const dialog_width = dialog_rect.right - dialog_rect.left;
    int const dialog_height = dialog_rect.bottom - dialog_rect.top;
    int const x = monitor_rect.left +
                  ((monitor_rect.right - monitor_rect.left) - dialog_width) / 2;
    int const y = monitor_rect.top +
                  ((monitor_rect.bottom - monitor_rect.top) - dialog_height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace

namespace greenflame {

AboutDialog::AboutDialog(HINSTANCE hinstance) : hinstance_(hinstance) {}

void AboutDialog::Show(HWND owner) const {
    INT_PTR const dialog_result = DialogBoxParamW(
        hinstance_, MAKEINTRESOURCEW(kAboutDialogResourceId), owner,
        &AboutDialog::Dialog_proc, reinterpret_cast<LPARAM>(hinstance_));
    if (dialog_result == -1) {
        MessageBoxW(owner, L"Failed to open About Greenflame dialog.", L"Greenflame",
                    MB_OK | MB_ICONWARNING);
    }
}

INT_PTR CALLBACK AboutDialog::Dialog_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                          LPARAM lparam) noexcept {
    switch (msg) {
    case WM_INITDIALOG: {
        HINSTANCE const hinstance = reinterpret_cast<HINSTANCE>(lparam);
        HICON const app_icon = static_cast<HICON>(
            LoadImageW(hinstance, MAKEINTRESOURCEW(kAppIconResourceId), IMAGE_ICON,
                       kAboutDialogIconSize, kAboutDialogIconSize, LR_DEFAULTCOLOR));
        if (app_icon != nullptr) {
            SendDlgItemMessageW(hwnd, kAboutDialogIconControlId, STM_SETICON,
                                reinterpret_cast<WPARAM>(app_icon), 0);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(app_icon));
        }
        Center_dialog_on_cursor_monitor(hwnd);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) {
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        return FALSE;
    case WM_DESTROY: {
        HICON const app_icon =
            reinterpret_cast<HICON>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (app_icon != nullptr) {
            DestroyIcon(app_icon);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return FALSE;
    }
    default:
        return FALSE;
    }
}

} // namespace greenflame
