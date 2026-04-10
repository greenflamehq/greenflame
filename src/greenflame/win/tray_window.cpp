// Tray window object: notification icon + context menu + PrintScreen hotkey.

#include "win/tray_window.h"
#include "app_config_store.h"
#include "win/about_dialog.h"
#include "win/debug_log.h"
#include "win/ui_palette.h"

namespace {

constexpr wchar_t kTrayWindowClass[] = L"GreenflameTray";
constexpr wchar_t kToastWindowClass[] = L"GreenflameToast";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kDeferredCopyWindowMessage = WM_APP + 2;
constexpr UINT kTrayIconId = 1;
constexpr UINT kModNoRepeat = 0x4000u;
constexpr UINT kDefaultDpi = 96;
constexpr int kAppIconResourceId = 1;
constexpr int kMaxDeferredCopyWindowRetries = 50;
constexpr BYTE kOpaqueAlpha = 0xFF;

enum CommandId : int {
    StartCapture = 1,
    CopyWindow = 2,
    CopyMonitor = 3,
    CopyDesktop = 4,
    CopyLastRegion = 5,
    CopyLastWindow = 6,
    IncludeCursor = 7,
    StartWithWindows = 8,
    OpenConfig = 9,
    About = 10,
    Exit = 11,
};

enum HotkeyId : int {
    HotkeyStartCapture = 1,
    HotkeyCopyWindow = 2,
    HotkeyCopyMonitor = 3,
    HotkeyCopyDesktop = 4,
    HotkeyCopyLastRegion = 5,
    HotkeyCopyLastWindow = 6,
    HotkeyTestingError = 90,
    HotkeyTestingWarning = 91,
};

struct HotkeyRegistrationSpec {
    int id = 0;
    UINT modifiers = 0;
    UINT virtual_key = 0;
    std::wstring_view display_name = {};
};

struct HotkeyRegistrationFailure {
    std::wstring_view display_name = {};
    DWORD error = 0;
    std::wstring error_message = {};
};

constexpr size_t kModifiedPrintScreenHotkeyCount = 5;
constexpr HotkeyRegistrationSpec kStartCaptureHotkey = {
    HotkeyStartCapture, kModNoRepeat, VK_SNAPSHOT, L"Print Screen"};

constexpr std::array<HotkeyRegistrationSpec, kModifiedPrintScreenHotkeyCount>
    kModifiedPrintScreenHotkeys = {{
        {HotkeyCopyWindow, static_cast<UINT>(MOD_CONTROL | kModNoRepeat), VK_SNAPSHOT,
         L"Ctrl + Prt Scrn"},
        {HotkeyCopyMonitor, static_cast<UINT>(MOD_SHIFT | kModNoRepeat), VK_SNAPSHOT,
         L"Shift + Prt Scrn"},
        {HotkeyCopyDesktop, static_cast<UINT>(MOD_CONTROL | MOD_SHIFT | kModNoRepeat),
         VK_SNAPSHOT, L"Ctrl + Shift + Prt Scrn"},
        {HotkeyCopyLastRegion, static_cast<UINT>(MOD_ALT | kModNoRepeat), VK_SNAPSHOT,
         L"Alt + Prt Scrn"},
        {HotkeyCopyLastWindow, static_cast<UINT>(MOD_CONTROL | MOD_ALT | kModNoRepeat),
         VK_SNAPSHOT, L"Ctrl + Alt + Prt Scrn"},
    }};

constexpr wchar_t kCaptureRegionMenuText[] = L"Capture region\tPrt Scrn";
constexpr wchar_t kCaptureMonitorMenuText[] =
    L"Capture current monitor\tShift + Prt Scrn";
constexpr wchar_t kCaptureWindowMenuText[] = L"Capture current window\tCtrl + Prt Scrn";
constexpr wchar_t kCaptureFullScreenMenuText[] =
    L"Capture full screen\tCtrl + Shift + Prt Scrn";
constexpr wchar_t kCaptureLastRegionMenuText[] = L"Capture last region\tAlt + Prt Scrn";
constexpr wchar_t kCaptureLastWindowMenuText[] =
    L"Capture last window\tCtrl + Alt + Prt Scrn";
constexpr wchar_t kIncludeCursorMenuText[] = L"Include captured cursor";
constexpr wchar_t kStartWithWindowsMenuText[] = L"Start with Windows";
constexpr wchar_t kOpenConfigMenuText[] = L"Open config file...";
constexpr wchar_t kAboutMenuText[] = L"About Greenflame...";

#ifdef DEBUG
constexpr wchar_t kTestingWarningBalloonMessage[] = L"Testing warning toast (Ctrl+W).";
constexpr wchar_t kTestingErrorBalloonMessage[] = L"Testing error toast (Ctrl+E).";
#endif

HWND s_last_foreground_hwnd = nullptr;
HWINEVENTHOOK s_foreground_hook = nullptr;

void CALLBACK Foreground_changed_hook(HWINEVENTHOOK, DWORD, HWND hwnd, LONG id_object,
                                      LONG id_child, DWORD, DWORD) noexcept {
    if (id_object != OBJID_WINDOW || id_child != 0) {
        return;
    }
    if (hwnd == nullptr || !IsWindowVisible(hwnd) || GetParent(hwnd) != nullptr) {
        return;
    }
    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 256);
    std::wstring_view const cls_sv(cls);
    if (cls_sv == L"NotifyIconOverflowWindow" || cls_sv == L"Shell_TrayWnd" ||
        cls_sv == L"Shell_SecondaryTrayWnd" || cls_sv == kTrayWindowClass) {
        return;
    }
    s_last_foreground_hwnd = hwnd;
}

[[nodiscard]] int Scale_for_dpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), static_cast<int>(kDefaultDpi));
}

[[nodiscard]] COLORREF Toast_accent_color(greenflame::TrayBalloonIcon icon) {
    switch (icon) {
    case greenflame::TrayBalloonIcon::Info:
        return greenflame::kToastAccentInfo;
    case greenflame::TrayBalloonIcon::Warning:
        return greenflame::kToastAccentWarning;
    case greenflame::TrayBalloonIcon::Error:
        return greenflame::kToastAccentError;
    }
    return greenflame::kToastAccentInfo;
}

bool Ensure_gdiplus() {
    // Tray toasts intentionally remain on GDI/GDI+; the D2D migration applies to
    // the fullscreen overlay renderer, not this small Win32 popup path.
    static ULONG_PTR token = 0;
    static bool ok = false;
    if (!ok) {
        Gdiplus::GdiplusStartupInput input;
        ok = Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
    }
    return ok;
}

Gdiplus::Color Gdiplus_color(COLORREF c, BYTE alpha = kOpaqueAlpha) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

// Icon glyphs are vector paths normalized to [0,1] and scaled at draw-time.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
void Draw_info_icon(HDC hdc, int x, int y, int size, COLORREF color) {
    if (!Ensure_gdiplus()) {
        return;
    }
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::SolidBrush fill(Gdiplus_color(color));
    auto const fx = static_cast<Gdiplus::REAL>(x);
    auto const fy = static_cast<Gdiplus::REAL>(y);
    auto const fs = static_cast<Gdiplus::REAL>(size - 1);
    g.FillEllipse(&fill, fx, fy, fs, fs);

    auto const stroke = static_cast<Gdiplus::REAL>(std::max(2, size / 8));
    Gdiplus::Pen pen(Gdiplus_color(greenflame::kToastIconGlyphLight), stroke);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    float size_f = static_cast<float>(size);
    float x_f = static_cast<float>(x);
    float y_f = static_cast<float>(y);
    Gdiplus::PointF check[3] = {
        {x_f + size_f * 0.25f, y_f + size_f * 0.52f},
        {x_f + size_f * 0.42f, y_f + size_f * 0.68f},
        {x_f + size_f * 0.75f, y_f + size_f * 0.32f},
    };
    g.DrawLines(&pen, check, 3);
}

void Draw_warning_icon(HDC hdc, int x, int y, int size, COLORREF color) {
    if (!Ensure_gdiplus()) {
        return;
    }
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    float x_f = static_cast<float>(x);
    float y_f = static_cast<float>(y);
    float size_f = static_cast<float>(size);
    Gdiplus::PointF triangle[3] = {
        {x_f + size_f * 0.5f, static_cast<Gdiplus::REAL>(y)},
        {static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + size)},
        {static_cast<Gdiplus::REAL>(x + size), static_cast<Gdiplus::REAL>(y + size)},
    };
    Gdiplus::SolidBrush fill(Gdiplus_color(color));
    g.FillPolygon(&fill, triangle, 3);

    Gdiplus::Color const bang_color = Gdiplus_color(greenflame::kToastIconGlyphWarning);
    auto const stroke = static_cast<Gdiplus::REAL>(std::max(2, size / 8));
    float const cx = x_f + size_f * 0.5f;

    float const nudge = size_f * 0.10f;
    Gdiplus::Pen stem_pen(bang_color, stroke);
    stem_pen.SetStartCap(Gdiplus::LineCapRound);
    stem_pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&stem_pen, cx, y_f + size_f * 0.35f + nudge, cx,
               y_f + size_f * 0.60f + nudge);

    float const dot_r = std::max(1.2f, size_f * 0.055f);
    float const dot_y = y_f + size_f * 0.72f + nudge;
    Gdiplus::SolidBrush dot_brush(bang_color);
    g.FillEllipse(&dot_brush, cx - dot_r, dot_y - dot_r, dot_r * 2, dot_r * 2);
}

void Draw_error_icon(HDC hdc, int x, int y, int size, COLORREF color) {
    if (!Ensure_gdiplus()) {
        return;
    }
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    float x_f = static_cast<float>(x);
    float y_f = static_cast<float>(y);
    float size_f = static_cast<float>(size);
    float const s = size_f * 0.30f;
    Gdiplus::PointF octagon[8] = {
        {x_f + s, static_cast<Gdiplus::REAL>(y)},
        {x_f + size_f - s, static_cast<Gdiplus::REAL>(y)},
        {static_cast<Gdiplus::REAL>(x_f + size_f), y_f + s},
        {static_cast<Gdiplus::REAL>(x_f + size_f), y_f + size_f - s},
        {x_f + size_f - s, static_cast<Gdiplus::REAL>(y + size)},
        {x_f + s, static_cast<Gdiplus::REAL>(y + size)},
        {static_cast<Gdiplus::REAL>(x_f), y_f + size_f - s},
        {static_cast<Gdiplus::REAL>(x_f), y_f + s},
    };
    Gdiplus::SolidBrush fill(Gdiplus_color(color));
    g.FillPolygon(&fill, octagon, 8);

    auto const stroke = static_cast<Gdiplus::REAL>(std::max(2, size / 8));
    float const m = size_f * 0.30f;
    Gdiplus::Pen pen(Gdiplus_color(greenflame::kToastIconGlyphLight), stroke);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&pen, x_f + m, y_f + m, x_f + size_f - m, y_f + size_f - m);
    g.DrawLine(&pen, x_f + size_f - m, y_f + m, x_f + m, y_f + size_f - m);
}
// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

void Draw_severity_icon(HDC hdc, int x, int y, int size,
                        greenflame::TrayBalloonIcon icon) {
    COLORREF const color = Toast_accent_color(icon);
    switch (icon) {
    case greenflame::TrayBalloonIcon::Info:
        Draw_info_icon(hdc, x, y, size, color);
        break;
    case greenflame::TrayBalloonIcon::Warning:
        Draw_warning_icon(hdc, x, y, size, color);
        break;
    case greenflame::TrayBalloonIcon::Error:
        Draw_error_icon(hdc, x, y, size, color);
        break;
    }
}

bool Open_file_in_default_editor(HWND owner, std::wstring_view path) {
    HINSTANCE const result = ShellExecuteW(nullptr, L"open", std::wstring(path).c_str(),
                                           nullptr, nullptr, SW_SHOW);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        MessageBoxW(owner, L"Could not open config file.", L"Greenflame",
                    MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

void Reveal_file_in_explorer(std::wstring_view path) {
    PIDLIST_ABSOLUTE const pidl = ILCreateFromPathW(std::wstring(path).c_str());
    if (pidl == nullptr) {
        return;
    }

    LPCITEMIDLIST const child = ILFindLastID(pidl);
    PIDLIST_ABSOLUTE const parent = ILClone(pidl);
    if (parent != nullptr) {
        ILRemoveLastID(parent);
        LPCITEMIDLIST child_items[] = {child};
        SHOpenFolderAndSelectItems(parent, 1, child_items, 0);
        ILFree(parent);
    }
    ILFree(pidl);
}

[[nodiscard]] int Measure_text_height(HDC hdc, HFONT font, std::wstring_view text,
                                      int width, DWORD flags) {
    if (hdc == nullptr || text.empty() || width <= 0) {
        return 0;
    }

    HGDIOBJ const old_font =
        SelectObject(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    RECT measure{};
    measure.right = width;
    DrawTextW(hdc, text.data(), static_cast<int>(text.size()), &measure,
              flags | DT_CALCRECT);
    SelectObject(hdc, old_font);
    return std::max(0, static_cast<int>(measure.bottom - measure.top));
}

[[nodiscard]] std::wstring Trim_trailing_wspace(std::wstring text) {
    while (!text.empty() && std::iswspace(text.back()) != 0) {
        text.pop_back();
    }
    return text;
}

[[nodiscard]] std::wstring Format_windows_error_message(DWORD error) {
    if (error == 0) {
        return L"Windows did not provide an error code.";
    }

    LPWSTR buffer = nullptr;
    DWORD const flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD const length =
        FormatMessageW(flags, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) {
        return L"Windows error " + std::to_wstring(error);
    }

    std::wstring message(buffer, static_cast<size_t>(length));
    LocalFree(buffer);
    return Trim_trailing_wspace(std::move(message));
}

void Log_hotkey_registration_failure(
    HotkeyRegistrationSpec const &hotkey,
    HotkeyRegistrationFailure const &failure) noexcept {
    std::wstring message = L"RegisterHotKey failed for ";
    message += hotkey.display_name;
    message += L" error=";
    message += std::to_wstring(failure.error);
    message += L" message=\"";
    message += failure.error_message;
    message += L"\" owner_process=unavailable";
    GREENFLAME_LOG_WRITE(L"hotkey", message);
}

[[nodiscard]] bool Try_register_hotkey(HWND hwnd, HotkeyRegistrationSpec const &hotkey,
                                       HotkeyRegistrationFailure &failure) {
    if (RegisterHotKey(hwnd, hotkey.id, hotkey.modifiers, hotkey.virtual_key) != 0) {
        return true;
    }

    failure.display_name = hotkey.display_name;
    failure.error = GetLastError();
    failure.error_message = Format_windows_error_message(failure.error);
    Log_hotkey_registration_failure(hotkey, failure);
    return false;
}

[[nodiscard]] std::wstring
Build_hotkey_registration_failure_message(std::wstring_view display_name,
                                          std::wstring_view error_message,
                                          std::wstring_view fallback_message) {
    std::wstring message(display_name);
    message += L" could not be registered.\nReason: ";
    message += error_message;
    message += L"\nWindows did not identify the owning process.\n";
    message += fallback_message;
    return message;
}

[[nodiscard]] std::wstring Build_modified_hotkey_registration_failure_message(
    std::span<HotkeyRegistrationFailure const> failures) {
    std::wstring message =
        L"The following modified Print Screen hotkeys could not be registered:";
    for (HotkeyRegistrationFailure const &failure : failures) {
        message += L"\n- ";
        message += failure.display_name;
        message += L" (";
        message += failure.error_message;
        message += L')';
    }
    message += L"\n\nWindows did not identify the owning process.\n"
               L"You can still use these commands from the tray menu.";
    return message;
}

} // namespace

namespace greenflame {

class TrayWindow::ToastPopup final {
  public:
    explicit ToastPopup(HINSTANCE hinstance) : hinstance_(hinstance) {}

    ~ToastPopup() {
        if (title_font_ != nullptr) {
            DeleteObject(title_font_);
        }
        if (body_font_ != nullptr) {
            DeleteObject(body_font_);
        }
        if (thumbnail_ != nullptr) {
            DeleteObject(thumbnail_);
        }
    }

    [[nodiscard]] static bool Register_window_class(HINSTANCE hinstance) {
        WNDCLASSEXW toast_class{};
        toast_class.cbSize = sizeof(toast_class);
        toast_class.style = CS_HREDRAW | CS_VREDRAW;
        toast_class.lpfnWndProc = &ToastPopup::Static_wnd_proc;
        toast_class.hInstance = hinstance;
        toast_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        toast_class.hbrBackground = nullptr;
        toast_class.lpszClassName = kToastWindowClass;
        return RegisterClassExW(&toast_class) != 0 ||
               GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    void Show(TrayBalloonIcon icon, wchar_t const *message, HBITMAP thumbnail,
              std::wstring_view file_path, ToastFileAction file_action,
              wchar_t const *detail_message, wchar_t const *footer_message) {
        if (!message || message[0] == L'\0') {
            return;
        }

        icon_ = icon;
        message_ = message;
        detail_message_ = detail_message ? detail_message : L"";
        file_path_ = std::wstring(file_path);
        file_action_ = file_action;
        footer_message_ = footer_message ? footer_message : L"";
        link_rect_ = {};
        mouse_over_link_ = false;

        if (thumbnail_ != nullptr) {
            DeleteObject(thumbnail_);
        }
        thumbnail_ = thumbnail;
        thumbnail_width_ = 0;
        thumbnail_height_ = 0;
        if (thumbnail_ != nullptr) {
            BITMAP bm{};
            if (GetObject(thumbnail_, sizeof(bm), &bm) != 0) {
                thumbnail_width_ = bm.bmWidth;
                thumbnail_height_ = bm.bmHeight;
            }
        }

        Ensure_window();
        if (!Is_open()) {
            return;
        }

        UINT dpi = GetDpiForWindow(hwnd_);
        if (dpi == 0) {
            dpi = kDefaultDpi;
        }
        Ensure_title_font(dpi);
        Ensure_body_font(dpi);

        int const margin = Scale_for_dpi(kMarginDip, dpi);

        HDC const hdc = GetDC(hwnd_);
        HDC const measure_dc = hdc ? hdc : GetDC(nullptr);
        ToastLayout const layout = Compute_layout(dpi, measure_dc);
        if (hdc != nullptr) {
            ReleaseDC(hwnd_, hdc);
        } else if (measure_dc != nullptr) {
            ReleaseDC(nullptr, measure_dc);
        }
        link_rect_ = layout.link_rect;

        POINT cursor{};
        GetCursorPos(&cursor);
        RECT work_area{};
        HMONITOR const monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info) != 0) {
            work_area = monitor_info.rcWork;
        } else if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0) == 0) {
            work_area.left = 0;
            work_area.top = 0;
            work_area.right = GetSystemMetrics(SM_CXSCREEN);
            work_area.bottom = GetSystemMetrics(SM_CYSCREEN);
        }

        int const x = work_area.right - layout.width - margin;
        int const y = work_area.bottom - layout.total_height - margin;
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, layout.width, layout.total_height,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        InvalidateRect(hwnd_, nullptr, TRUE);
        KillTimer(hwnd_, kTimerId);
        if (!mouse_inside_) {
            SetTimer(hwnd_, kTimerId, kDurationMs, nullptr);
        }
    }

    void Destroy() {
        if (!Is_open()) {
            return;
        }
        KillTimer(hwnd_, kTimerId);
        DestroyWindow(hwnd_);
    }

  private:
    struct ToastLayout {
        int padding;
        int accent_bar_width;
        int header_gap;
        int icon_size;
        int icon_gap;
        int title_app_icon_size;
        int title_icon_text_gap;
        int thumbnail_max_height;
        int thumbnail_gap;
        int section_gap;
        int link_gap;
        int width;
        int content_left;
        int content_right;
        int content_width;
        int title_left;
        int title_text_left;
        int title_right;
        int text_left;
        int text_width;
        int body_top;
        int body_row_height;
        int summary_height;
        int detail_height;
        int link_height;
        int footer_top;
        int footer_height;
        int total_height;
        RECT link_rect; // zeroed when file_path_ is empty
    };

    [[nodiscard]] ToastLayout Compute_layout(UINT dpi, HDC hdc) const;

    static constexpr UINT_PTR kTimerId = 1;
    static constexpr UINT kDurationMs = 5000;
    static constexpr int kWidthDip = 340;
    static constexpr int kMarginDip = 18;
    static constexpr int kPaddingDip = 14;
    static constexpr int kAccentBarWidthDip = 4;
    static constexpr int kHeaderGapDip = 11;
    static constexpr int kIconDip = 20;
    static constexpr int kIconGapDip = 10;
    static constexpr int kTitleAppIconDip = 14;
    static constexpr int kTitleIconTextGapDip =
        kTitleAppIconDip; // gap intentionally matches icon width
    static constexpr int kTitleFontDip = 12;
    static constexpr int kBodyFontDip = 13;
    static constexpr int kMinHeightDip = 56;
    static constexpr int kMaxHeightDip = 280;
    static constexpr int kFallbackTextHeightDip = 18;
    static constexpr int kThumbnailMaxHeightDip = 80;
    static constexpr int kThumbnailGapDip = 8;
    static constexpr int kSectionGapDip = 4;
    static constexpr int kLinkGapDip = 6;
    static constexpr wchar_t kTitleText[] = L"Greenflame";

    [[nodiscard]] bool Is_open() const {
        return hwnd_ != nullptr && IsWindow(hwnd_) != 0;
    }

    void Ensure_title_font(UINT dpi) {
        if (title_font_ != nullptr && title_font_dpi_ == dpi) {
            return;
        }
        if (title_font_ != nullptr) {
            DeleteObject(title_font_);
            title_font_ = nullptr;
        }
        int const h = -Scale_for_dpi(kTitleFontDip, dpi);
        title_font_ =
            CreateFontW(h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                        FF_DONTCARE, L"Segoe UI");
        title_font_dpi_ = dpi;
    }

    void Ensure_body_font(UINT dpi) {
        if (body_font_ != nullptr && body_font_dpi_ == dpi) {
            return;
        }
        if (body_font_ != nullptr) {
            DeleteObject(body_font_);
            body_font_ = nullptr;
        }
        int const h = -Scale_for_dpi(kBodyFontDip, dpi);
        body_font_ =
            CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                        FF_DONTCARE, L"Segoe UI");
        body_font_dpi_ = dpi;
    }

    void Hide() {
        if (Is_open()) {
            ShowWindow(hwnd_, SW_HIDE);
        }
    }

    void Ensure_window() {
        if (Is_open()) {
            return;
        }
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                kToastWindowClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr,
                                nullptr, hinstance_, this);
    }

    static LRESULT CALLBACK Static_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                            LPARAM lparam) {
        if (msg == WM_NCCREATE) {
            CREATESTRUCTW const *create =
                reinterpret_cast<CREATESTRUCTW const *>(lparam);
            ToastPopup *self = reinterpret_cast<ToastPopup *>(create->lpCreateParams);
            if (!self) {
                return FALSE;
            }
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }

        ToastPopup *self =
            reinterpret_cast<ToastPopup *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

    LRESULT Wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam) {
        switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            if (!mouse_inside_) {
                mouse_inside_ = true;
                KillTimer(hwnd_, kTimerId);
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd_;
                TrackMouseEvent(&tme);
            }
            if (!file_path_.empty() && !IsRectEmpty(&link_rect_)) {
                POINT const pt{static_cast<int>(static_cast<short>(LOWORD(lparam))),
                               static_cast<int>(static_cast<short>(HIWORD(lparam)))};
                bool const over_link = PtInRect(&link_rect_, pt) != 0;
                if (over_link != mouse_over_link_) {
                    mouse_over_link_ = over_link;
                    InvalidateRect(hwnd_, &link_rect_, FALSE);
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            mouse_inside_ = false;
            if (mouse_over_link_) {
                mouse_over_link_ = false;
                InvalidateRect(hwnd_, &link_rect_, FALSE);
            }
            SetTimer(hwnd_, kTimerId, kDurationMs, nullptr);
            return 0;
        case WM_LBUTTONUP: {
            if (!file_path_.empty() && !IsRectEmpty(&link_rect_)) {
                POINT const pt{static_cast<int>(static_cast<short>(LOWORD(lparam))),
                               static_cast<int>(static_cast<short>(HIWORD(lparam)))};
                if (PtInRect(&link_rect_, pt)) {
                    KillTimer(hwnd_, kTimerId);
                    Hide();
                    if (file_action_ == ToastFileAction::OpenFile) {
                        (void)Open_file_in_default_editor(hwnd_, file_path_);
                    } else {
                        Reveal_file_in_explorer(file_path_);
                    }
                }
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT && !file_path_.empty() &&
                !IsRectEmpty(&link_rect_)) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd_, &pt);
                if (PtInRect(&link_rect_, pt)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            return DefWindowProcW(hwnd_, msg, wparam, lparam);
        case WM_TIMER:
            if (wparam == kTimerId) {
                KillTimer(hwnd_, kTimerId);
                Hide();
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC const hdc = BeginPaint(hwnd_, &paint);
            if (hdc != nullptr) {
                RECT client{};
                GetClientRect(hwnd_, &client);

                UINT dpi = GetDpiForWindow(hwnd_);
                if (dpi == 0) {
                    dpi = kDefaultDpi;
                }
                Ensure_title_font(dpi);
                Ensure_body_font(dpi);

                ToastLayout const layout = Compute_layout(dpi, hdc);
                link_rect_ = layout.link_rect;

                COLORREF const background_color = kToastBackground;
                COLORREF const border_color = kToastBorder;
                COLORREF const title_color = kToastTitleText;
                COLORREF const text_color = kToastBodyText;
                COLORREF const accent_color = Toast_accent_color(icon_);

                HBRUSH const bg_brush = CreateSolidBrush(background_color);
                if (bg_brush != nullptr) {
                    FillRect(hdc, &client, bg_brush);
                    DeleteObject(bg_brush);
                }

                RECT accent_bar = client;
                accent_bar.right = accent_bar.left + layout.accent_bar_width;
                HBRUSH const accent_brush = CreateSolidBrush(accent_color);
                if (accent_brush != nullptr) {
                    FillRect(hdc, &accent_bar, accent_brush);
                    DeleteObject(accent_brush);
                }

                HBRUSH const border_brush = CreateSolidBrush(border_color);
                if (border_brush != nullptr) {
                    FrameRect(hdc, &client, border_brush);
                    DeleteObject(border_brush);
                }

                RECT title_rect{};
                title_rect.left = layout.title_text_left;
                title_rect.top = layout.padding;
                title_rect.right = layout.title_right;
                title_rect.bottom = title_rect.top + layout.title_app_icon_size;

                HICON loaded_app_icon = static_cast<HICON>(
                    LoadImageW(hinstance_, MAKEINTRESOURCEW(kAppIconResourceId),
                               IMAGE_ICON, layout.title_app_icon_size,
                               layout.title_app_icon_size, LR_DEFAULTCOLOR));
                if (loaded_app_icon != nullptr) {
                    int const app_icon_x = layout.title_left;
                    int const app_icon_y =
                        title_rect.top + ((title_rect.bottom - title_rect.top -
                                           layout.title_app_icon_size) /
                                          2);
                    DrawIconEx(hdc, app_icon_x, app_icon_y, loaded_app_icon,
                               layout.title_app_icon_size, layout.title_app_icon_size,
                               0, nullptr, DI_NORMAL);
                    DestroyIcon(loaded_app_icon);
                }

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, title_color);
                HGDIOBJ const old_font = SelectObject(
                    hdc, title_font_ ? title_font_ : GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(hdc, kTitleText, -1, &title_rect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
                              DT_END_ELLIPSIS);

                Draw_severity_icon(hdc, layout.content_left, layout.body_top,
                                   layout.icon_size, icon_);

                RECT body_rect{};
                body_rect.left = layout.text_left;
                body_rect.top = layout.body_top;
                body_rect.right = layout.content_right;
                body_rect.bottom = client.bottom - layout.padding;

                SetTextColor(hdc, text_color);
                SelectObject(hdc, body_font_ ? body_font_
                                             : GetStockObject(DEFAULT_GUI_FONT));

                int body_cursor_top = layout.body_top;
                if (!message_.empty()) {
                    RECT summary_rect = body_rect;
                    summary_rect.top = body_cursor_top;
                    summary_rect.bottom = summary_rect.top + layout.summary_height;
                    DrawTextW(hdc, message_.c_str(), -1, &summary_rect,
                              DT_WORDBREAK | DT_NOPREFIX);
                    body_cursor_top = summary_rect.bottom;
                }

                if (!detail_message_.empty()) {
                    if (!message_.empty()) {
                        body_cursor_top += layout.section_gap;
                    }
                    RECT detail_rect = body_rect;
                    detail_rect.top = body_cursor_top;
                    detail_rect.bottom = detail_rect.top + layout.detail_height;
                    DrawTextW(hdc, detail_message_.c_str(), -1, &detail_rect,
                              DT_WORDBREAK | DT_NOPREFIX);
                }

                // Bottom of body content (text or icon, whichever is taller).
                int anchor_bottom = layout.body_top + layout.body_row_height;

                // Draw the file path as a clickable link when present.
                if (!file_path_.empty()) {
                    RECT link_draw_rect = layout.link_rect;

                    SetTextColor(hdc, kToastLinkText);
                    DrawTextW(hdc, file_path_.c_str(), -1, &link_draw_rect,
                              DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    if (mouse_over_link_) {
                        SIZE text_sz{};
                        GetTextExtentPoint32W(hdc, file_path_.c_str(),
                                              static_cast<int>(file_path_.size()),
                                              &text_sz);
                        int const underline_right =
                            link_draw_rect.left +
                            std::min(text_sz.cx,
                                     link_draw_rect.right - link_draw_rect.left);
                        HPEN const link_pen = CreatePen(PS_SOLID, 1, kToastLinkText);
                        if (link_pen != nullptr) {
                            HGDIOBJ const old_pen = SelectObject(hdc, link_pen);
                            MoveToEx(hdc, link_draw_rect.left,
                                     link_draw_rect.bottom - 1, nullptr);
                            LineTo(hdc, underline_right, link_draw_rect.bottom - 1);
                            SelectObject(hdc, old_pen);
                            DeleteObject(link_pen);
                        }
                    }

                    anchor_bottom = link_draw_rect.bottom;
                }

                if (!footer_message_.empty()) {
                    RECT footer_rect = body_rect;
                    footer_rect.top = layout.footer_top;
                    footer_rect.bottom = footer_rect.top + layout.footer_height;
                    SetTextColor(hdc, text_color);
                    DrawTextW(hdc, footer_message_.c_str(), -1, &footer_rect,
                              DT_WORDBREAK | DT_NOPREFIX);
                    anchor_bottom = footer_rect.bottom;
                }

                SelectObject(hdc, old_font);

                if (thumbnail_ != nullptr && thumbnail_width_ > 0 &&
                    thumbnail_height_ > 0) {

                    float const scale_w = static_cast<float>(layout.content_width) /
                                          static_cast<float>(thumbnail_width_);
                    float const scale_h =
                        static_cast<float>(layout.thumbnail_max_height) /
                        static_cast<float>(thumbnail_height_);
                    float scale = (std::min)(scale_w, scale_h);
                    if (scale > 1.0f) {
                        scale = 1.0f;
                    }
                    int const thumb_w =
                        static_cast<int>(static_cast<float>(thumbnail_width_) * scale);
                    int const thumb_h =
                        static_cast<int>(static_cast<float>(thumbnail_height_) * scale);
                    int const thumb_top = anchor_bottom + layout.thumbnail_gap;
                    int const thumb_left = layout.content_left;

                    RECT thumb_border_rect{};
                    thumb_border_rect.left = thumb_left - 1;
                    thumb_border_rect.top = thumb_top - 1;
                    thumb_border_rect.right = thumb_left + thumb_w + 1;
                    thumb_border_rect.bottom = thumb_top + thumb_h + 1;
                    HBRUSH const thumb_border_brush = CreateSolidBrush(border_color);
                    if (thumb_border_brush != nullptr) {
                        FrameRect(hdc, &thumb_border_rect, thumb_border_brush);
                        DeleteObject(thumb_border_brush);
                    }

                    HDC const thumb_dc = CreateCompatibleDC(hdc);
                    if (thumb_dc != nullptr) {
                        HGDIOBJ const old_bmp = SelectObject(thumb_dc, thumbnail_);
                        SetStretchBltMode(hdc, HALFTONE);
                        SetBrushOrgEx(hdc, 0, 0, nullptr);
                        StretchBlt(hdc, thumb_left, thumb_top, thumb_w, thumb_h,
                                   thumb_dc, 0, 0, thumbnail_width_, thumbnail_height_,
                                   SRCCOPY);
                        SelectObject(thumb_dc, old_bmp);
                        DeleteDC(thumb_dc);
                    }
                }
            }
            EndPaint(hwnd_, &paint);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd_, msg, wparam, lparam);
        }
    }

    HINSTANCE hinstance_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT title_font_ = nullptr;
    HFONT body_font_ = nullptr;
    UINT title_font_dpi_ = 0;
    UINT body_font_dpi_ = 0;
    std::wstring message_ = {};
    std::wstring detail_message_ = {};
    std::wstring file_path_ = {};
    ToastFileAction file_action_ = ToastFileAction::RevealInExplorer;
    std::wstring footer_message_ = {};
    HBITMAP thumbnail_ = nullptr;
    TrayBalloonIcon icon_ = TrayBalloonIcon::Info;
    int thumbnail_width_ = 0;
    int thumbnail_height_ = 0;
    RECT link_rect_ = {};
    bool mouse_inside_ = false;
    bool mouse_over_link_ = false;
};

TrayWindow::ToastPopup::ToastLayout
TrayWindow::ToastPopup::Compute_layout(UINT dpi, HDC hdc) const {
    ToastLayout l{};
    l.padding = Scale_for_dpi(kPaddingDip, dpi);
    l.accent_bar_width = Scale_for_dpi(kAccentBarWidthDip, dpi);
    l.header_gap = Scale_for_dpi(kHeaderGapDip, dpi);
    l.icon_size = Scale_for_dpi(kIconDip, dpi);
    l.icon_gap = Scale_for_dpi(kIconGapDip, dpi);
    l.title_app_icon_size = Scale_for_dpi(kTitleAppIconDip, dpi);
    l.title_icon_text_gap = Scale_for_dpi(kTitleIconTextGapDip, dpi);
    l.thumbnail_max_height = Scale_for_dpi(kThumbnailMaxHeightDip, dpi);
    l.thumbnail_gap = Scale_for_dpi(kThumbnailGapDip, dpi);
    l.section_gap = Scale_for_dpi(kSectionGapDip, dpi);
    l.link_gap = Scale_for_dpi(kLinkGapDip, dpi);
    l.width = Scale_for_dpi(kWidthDip, dpi);

    l.content_left = l.accent_bar_width + l.padding;
    l.content_right = l.width - l.padding;
    l.content_width = l.content_right - l.content_left;
    l.title_left = l.content_left;
    l.title_text_left = l.title_left + l.title_app_icon_size + l.title_icon_text_gap;
    l.title_right = l.content_right;
    if (l.title_right <= l.title_text_left) {
        l.title_right = l.title_text_left + 1;
    }
    l.text_left = l.content_left + l.icon_size + l.icon_gap;
    l.text_width = std::max(1, l.content_right - l.text_left);

    // Text measurements.
    int title_height = l.title_app_icon_size;
    int body_height = 0;

    HGDIOBJ const old_font =
        SelectObject(hdc, title_font_ ? title_font_ : GetStockObject(DEFAULT_GUI_FONT));

    RECT title_measure{};
    title_measure.right = l.title_right - l.title_text_left;
    DrawTextW(hdc, kTitleText, -1, &title_measure,
              DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_CALCRECT);
    int const measured_title = title_measure.bottom - title_measure.top;
    if (measured_title > title_height) {
        title_height = measured_title;
    }

    SelectObject(hdc, old_font);

    DWORD const wrapped_flags = DT_WORDBREAK | DT_NOPREFIX;
    DWORD const single_line_flags = DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    l.summary_height =
        Measure_text_height(hdc, body_font_, message_, l.text_width, wrapped_flags);
    l.detail_height = Measure_text_height(hdc, body_font_, detail_message_,
                                          l.text_width, wrapped_flags);
    l.link_height = Measure_text_height(hdc, body_font_, file_path_, l.text_width,
                                        single_line_flags);
    if (!file_path_.empty() && l.link_height <= 0) {
        l.link_height = Scale_for_dpi(kBodyFontDip, dpi);
    }
    l.footer_height = Measure_text_height(hdc, body_font_, footer_message_,
                                          l.text_width, wrapped_flags);
    l.footer_top = 0;

    if (l.summary_height <= 0 && !message_.empty()) {
        l.summary_height = Scale_for_dpi(kFallbackTextHeightDip, dpi);
    }

    body_height += l.summary_height;
    if (l.detail_height > 0) {
        if (body_height > 0) {
            body_height += l.section_gap;
        }
        body_height += l.detail_height;
    }
    if (body_height <= 0) {
        body_height = Scale_for_dpi(kFallbackTextHeightDip, dpi);
    }

    l.body_row_height = std::max(l.icon_size, body_height);
    l.body_top = l.padding + l.title_app_icon_size + l.header_gap;

    // Compute link_rect when a file path is present.
    l.link_rect = {};
    int content_bottom = l.body_top + l.body_row_height;
    if (!file_path_.empty()) {
        int const link_top = content_bottom + l.link_gap;
        l.link_rect = {l.text_left, link_top, l.content_right,
                       link_top + l.link_height};
        content_bottom = l.link_rect.bottom;
    }
    if (!footer_message_.empty()) {
        l.footer_top = content_bottom + l.link_gap;
    }

    // Total height.
    int height = l.padding + title_height + l.header_gap + l.body_row_height;
    if (!file_path_.empty()) {
        height += l.link_gap + l.link_height;
    }
    if (!footer_message_.empty()) {
        height += l.link_gap + l.footer_height;
    }
    height += l.padding;

    if (thumbnail_ != nullptr && thumbnail_width_ > 0 && thumbnail_height_ > 0) {
        float const scale_w =
            static_cast<float>(l.content_width) / static_cast<float>(thumbnail_width_);
        float const scale_h = static_cast<float>(l.thumbnail_max_height) /
                              static_cast<float>(thumbnail_height_);
        float scale = (std::min)(scale_w, scale_h);
        if (scale > 1.0f) {
            scale = 1.0f;
        }
        height += l.thumbnail_gap +
                  static_cast<int>(static_cast<float>(thumbnail_height_) * scale);
    }

    int const min_height = Scale_for_dpi(kMinHeightDip, dpi);
    int const max_height = Scale_for_dpi(kMaxHeightDip, dpi);
    l.total_height = std::clamp(height, min_height, max_height);
    return l;
}

TrayWindow::TrayWindow(ITrayEvents *events) : events_(events) {}

TrayWindow::~TrayWindow() { Destroy(); }

bool TrayWindow::Register_window_class(HINSTANCE hinstance) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &TrayWindow::Static_wnd_proc;
    window_class.hInstance = hinstance;
    window_class.lpszClassName = kTrayWindowClass;
    bool const tray_registered = RegisterClassExW(&window_class) != 0 ||
                                 GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    if (!tray_registered) {
        return false;
    }
    return ToastPopup::Register_window_class(hinstance);
}

bool TrayWindow::Create(HINSTANCE hinstance, bool enable_testing_hotkeys,
                        bool start_with_windows_enabled) {
    if (Is_open()) {
        return true;
    }
    hinstance_ = hinstance;
    testing_hotkeys_enabled_ = enable_testing_hotkeys;
    start_with_windows_enabled_ = start_with_windows_enabled;
    HWND const hwnd = CreateWindowExW(0, kTrayWindowClass, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, hinstance, this);
    if (!hwnd) {
        hinstance_ = nullptr;
        return false;
    }

    NOTIFYICONDATAW notify_data{};
    notify_data.cbSize = sizeof(notify_data);
    notify_data.hWnd = hwnd;
    notify_data.uID = kTrayIconId;
    notify_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    notify_data.uCallbackMessage = kTrayCallbackMessage;
    notify_data.hIcon = LoadIconW(hinstance, MAKEINTRESOURCEW(kAppIconResourceId));
    {
        static constexpr std::wstring_view kTip(L"Greenflame");
        std::copy_n(kTip.data(), kTip.size(), notify_data.szTip);
    }
    if (!Shell_NotifyIconW(NIM_ADD, &notify_data)) {
        DestroyWindow(hwnd);
        return false;
    }

    toast_popup_ = std::make_unique<ToastPopup>(hinstance_);

    for (;;) {
        HotkeyRegistrationFailure failure = {};
        if (Try_register_hotkey(hwnd, kStartCaptureHotkey, failure)) {
            break;
        }
        std::wstring const message = Build_hotkey_registration_failure_message(
            kStartCaptureHotkey.display_name, failure.error_message,
            L"You can still start capture from the tray menu.");
        int const user_choice = MessageBoxW(hwnd, message.c_str(), L"Greenflame",
                                            MB_ABORTRETRYIGNORE | MB_ICONWARNING);
        if (user_choice == IDRETRY) {
            continue;
        }
        break;
    }

    std::vector<HotkeyRegistrationFailure> modified_hotkey_failures = {};
    modified_hotkey_failures.reserve(kModifiedPrintScreenHotkeys.size());
    for (HotkeyRegistrationSpec const &hotkey : kModifiedPrintScreenHotkeys) {
        HotkeyRegistrationFailure failure = {};
        if (!Try_register_hotkey(hwnd, hotkey, failure)) {
            modified_hotkey_failures.push_back(std::move(failure));
        }
    }
    if (!modified_hotkey_failures.empty()) {
        std::wstring const message = Build_modified_hotkey_registration_failure_message(
            std::span<HotkeyRegistrationFailure const>(modified_hotkey_failures));
        MessageBoxW(hwnd, message.c_str(), L"Greenflame", MB_OK | MB_ICONWARNING);
    }

    if (testing_hotkeys_enabled_) {
        RegisterHotKey(hwnd, HotkeyTestingError,
                       static_cast<UINT>(MOD_CONTROL | kModNoRepeat), L'E');
        RegisterHotKey(hwnd, HotkeyTestingWarning,
                       static_cast<UINT>(MOD_CONTROL | kModNoRepeat), L'W');
    }

    s_foreground_hook =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                        Foreground_changed_hook, 0, 0, WINEVENT_OUTOFCONTEXT);
    return true;
}

void TrayWindow::Destroy() {
    if (!Is_open()) {
        return;
    }
    if (toast_popup_) {
        toast_popup_->Destroy();
    }
    DestroyWindow(hwnd_);
}

bool TrayWindow::Is_open() const { return hwnd_ != nullptr && IsWindow(hwnd_) != 0; }

void TrayWindow::Show_balloon(TrayBalloonIcon icon, wchar_t const *message,
                              HBITMAP thumbnail, std::wstring_view file_path,
                              ToastFileAction file_action,
                              wchar_t const *detail_message,
                              wchar_t const *footer_message) {
    if (!Is_open() || !message || message[0] == L'\0') {
        if (thumbnail != nullptr) {
            DeleteObject(thumbnail);
        }
        return;
    }
    if (!toast_popup_) {
        toast_popup_ = std::make_unique<ToastPopup>(hinstance_);
    }
    toast_popup_->Show(icon, message, thumbnail, file_path, file_action, detail_message,
                       footer_message);
}

LRESULT CALLBACK TrayWindow::Static_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                             LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW const *create = reinterpret_cast<CREATESTRUCTW const *>(lparam);
        TrayWindow *self = reinterpret_cast<TrayWindow *>(create->lpCreateParams);
        if (!self) {
            return FALSE;
        }
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }

    TrayWindow *self =
        reinterpret_cast<TrayWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    LRESULT const result = self->Wnd_proc(msg, wparam, lparam);
    if (msg == WM_NCDESTROY) {
        self->hwnd_ = nullptr;
        self->toast_popup_.reset();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return result;
}

LRESULT TrayWindow::Wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_COMMAND: {
        switch (static_cast<CommandId>(LOWORD(wparam))) {
        case StartCapture:
            Notify_start_capture();
            break;
        case CopyWindow:
            PostMessage(hwnd_, kDeferredCopyWindowMessage, 0, 0);
            break;
        case CopyMonitor:
            Notify_copy_monitor_to_clipboard();
            break;
        case CopyDesktop:
            Notify_copy_desktop_to_clipboard();
            break;
        case CopyLastRegion:
            Notify_copy_last_region_to_clipboard();
            break;
        case CopyLastWindow:
            Notify_copy_last_window_to_clipboard();
            break;
        case IncludeCursor:
            Notify_toggle_include_cursor();
            break;
        case StartWithWindows:
            Notify_toggle_start_with_windows();
            break;
        case OpenConfig:
            Open_config_file();
            break;
        case About:
            Show_about_dialog();
            break;
        case Exit:
            if (events_) events_->On_exit_requested();
            break;
        }
        return 0;
    }
    case WM_HOTKEY:
        switch (static_cast<HotkeyId>(wparam)) {
        case HotkeyStartCapture:
            Notify_start_capture();
            break;
        case HotkeyCopyWindow:
            Notify_copy_window_to_clipboard();
            break;
        case HotkeyCopyMonitor:
            Notify_copy_monitor_to_clipboard();
            break;
        case HotkeyCopyDesktop:
            Notify_copy_desktop_to_clipboard();
            break;
        case HotkeyCopyLastRegion:
            Notify_copy_last_region_to_clipboard();
            break;
        case HotkeyCopyLastWindow:
            Notify_copy_last_window_to_clipboard();
            break;
#ifdef DEBUG
        case HotkeyTestingError:
            if (testing_hotkeys_enabled_) {
                Show_balloon(TrayBalloonIcon::Error, kTestingErrorBalloonMessage);
            }
            break;
        case HotkeyTestingWarning:
            if (testing_hotkeys_enabled_) {
                Show_balloon(TrayBalloonIcon::Warning, kTestingWarningBalloonMessage);
            }
            break;
#else
        case HotkeyTestingError:
        case HotkeyTestingWarning:
            break;
#endif
        }
        return 0;
    case WM_DESTROY: {
        if (s_foreground_hook) {
            UnhookWinEvent(s_foreground_hook);
            s_foreground_hook = nullptr;
        }
        if (toast_popup_) {
            toast_popup_->Destroy();
        }
        UnregisterHotKey(hwnd_, HotkeyStartCapture);
        UnregisterHotKey(hwnd_, HotkeyCopyWindow);
        UnregisterHotKey(hwnd_, HotkeyCopyMonitor);
        UnregisterHotKey(hwnd_, HotkeyCopyDesktop);
        UnregisterHotKey(hwnd_, HotkeyCopyLastRegion);
        UnregisterHotKey(hwnd_, HotkeyCopyLastWindow);
        UnregisterHotKey(hwnd_, HotkeyTestingError);
        UnregisterHotKey(hwnd_, HotkeyTestingWarning);
        NOTIFYICONDATAW notify_data{};
        notify_data.cbSize = sizeof(notify_data);
        notify_data.hWnd = hwnd_;
        notify_data.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &notify_data);
        PostQuitMessage(0);
        return 0;
    }
    case WM_NCDESTROY:
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    default:
        if (msg == kTrayCallbackMessage) {
            if (LOWORD(lparam) == WM_LBUTTONUP) {
                Notify_start_capture();
            } else if (LOWORD(lparam) == WM_RBUTTONUP) {
                Show_context_menu();
            }
            return 0;
        }
        if (msg == kDeferredCopyWindowMessage) {
            HWND overflow = FindWindowW(L"NotifyIconOverflowWindow", nullptr);
            int retries = static_cast<int>(wparam);
            if (overflow != nullptr && IsWindowVisible(overflow) &&
                retries < kMaxDeferredCopyWindowRetries) {
                WPARAM const next_retry = static_cast<WPARAM>(retries) + 1;
                PostMessage(hwnd_, kDeferredCopyWindowMessage, next_retry, 0);
                return 0;
            }
            Notify_copy_window_to_clipboard();
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

void TrayWindow::Show_context_menu() {
    HMENU const menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    AppendMenuW(menu, MF_STRING, StartCapture, kCaptureRegionMenuText);
    AppendMenuW(menu, MF_STRING, CopyMonitor, kCaptureMonitorMenuText);
    AppendMenuW(menu, MF_STRING, CopyWindow, kCaptureWindowMenuText);
    AppendMenuW(menu, MF_STRING, CopyDesktop, kCaptureFullScreenMenuText);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CopyLastRegion, kCaptureLastRegionMenuText);
    AppendMenuW(menu, MF_STRING, CopyLastWindow, kCaptureLastWindowMenuText);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    UINT include_cursor_flags = MF_STRING;
    if (events_ != nullptr && events_->Is_include_cursor_enabled()) {
        include_cursor_flags |= MF_CHECKED;
    }
    AppendMenuW(menu, include_cursor_flags, IncludeCursor, kIncludeCursorMenuText);
    UINT start_with_windows_flags = MF_STRING;
    if (start_with_windows_enabled_) {
        start_with_windows_flags |= MF_CHECKED;
    }
    AppendMenuW(menu, start_with_windows_flags, StartWithWindows,
                kStartWithWindowsMenuText);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, OpenConfig, kOpenConfigMenuText);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, About, kAboutMenuText);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Exit, L"Exit");
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd_);
    TrackPopupMenuEx(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, cursor.x,
                     cursor.y, hwnd_, nullptr);
    DestroyMenu(menu);
}

void TrayWindow::Show_about_dialog() {
    if (!Is_open()) {
        return;
    }
    AboutDialog about_dialog(hinstance_);
    about_dialog.Show(hwnd_);
}

void TrayWindow::Open_config_file() {
    std::filesystem::path path = Get_config_file_path();
    if (path.empty()) {
        return;
    }
    if (!std::filesystem::exists(path)) {
        core::AppConfig const defaults{};
        (void)Save_app_config(defaults);
    }
    (void)Open_file_in_default_editor(hwnd_, path.wstring());
}

void TrayWindow::Notify_start_capture() {
    if (events_) {
        events_->On_start_capture_requested();
    }
}

void TrayWindow::Notify_copy_window_to_clipboard() {
    if (events_) {
        events_->On_copy_window_to_clipboard_requested(s_last_foreground_hwnd);
    }
}

void TrayWindow::Notify_copy_monitor_to_clipboard() {
    if (events_) {
        events_->On_copy_monitor_to_clipboard_requested();
    }
}

void TrayWindow::Notify_copy_desktop_to_clipboard() {
    if (events_) {
        events_->On_copy_desktop_to_clipboard_requested();
    }
}

void TrayWindow::Notify_copy_last_region_to_clipboard() {
    if (events_) {
        events_->On_copy_last_region_to_clipboard_requested();
    }
}

void TrayWindow::Notify_copy_last_window_to_clipboard() {
    if (events_) {
        events_->On_copy_last_window_to_clipboard_requested();
    }
}

void TrayWindow::Notify_toggle_include_cursor() {
    if (!events_) {
        return;
    }
    bool const desired_state = !events_->Is_include_cursor_enabled();
    (void)events_->On_set_include_cursor_enabled(desired_state);
}

void TrayWindow::Notify_toggle_start_with_windows() {
    if (!events_) {
        return;
    }
    bool const desired_state = !start_with_windows_enabled_;
    if (events_->On_set_start_with_windows_enabled(desired_state)) {
        start_with_windows_enabled_ = desired_state;
    }
}

} // namespace greenflame
