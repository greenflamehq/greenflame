// Tray window object: notification icon + context menu + PrintScreen hotkey.

#include "win/tray_window.h"
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
    Exit = 7,
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

constexpr wchar_t kCaptureRegionMenuText[] = L"Capture region\tPrt Scrn";
constexpr wchar_t kCaptureMonitorMenuText[] =
    L"Capture current monitor\tShift + Prt Scrn";
constexpr wchar_t kCaptureWindowMenuText[] = L"Capture current window\tCtrl + Prt Scrn";
constexpr wchar_t kCaptureFullScreenMenuText[] =
    L"Capture full screen\tCtrl + Shift + Prt Scrn";
constexpr wchar_t kCaptureLastRegionMenuText[] = L"Capture last region\tAlt + Prt Scrn";
constexpr wchar_t kCaptureLastWindowMenuText[] =
    L"Capture last window\tCtrl + Alt + Prt Scrn";

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
        return greenflame::winui::kToastAccentInfo;
    case greenflame::TrayBalloonIcon::Warning:
        return greenflame::winui::kToastAccentWarning;
    case greenflame::TrayBalloonIcon::Error:
        return greenflame::winui::kToastAccentError;
    }
    return greenflame::winui::kToastAccentInfo;
}

bool Ensure_gdiplus() {
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
    Gdiplus::Pen pen(Gdiplus_color(greenflame::winui::kToastIconGlyphLight), stroke);
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

    Gdiplus::Color const bang_color =
        Gdiplus_color(greenflame::winui::kToastIconGlyphWarning);
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
    Gdiplus::Pen pen(Gdiplus_color(greenflame::winui::kToastIconGlyphLight), stroke);
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

    void Show(TrayBalloonIcon icon, wchar_t const *message, HBITMAP thumbnail) {
        if (!message || message[0] == L'\0') {
            return;
        }

        icon_ = icon;
        message_ = message;

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

        int const width = Scale_for_dpi(kWidthDip, dpi);
        int const margin = Scale_for_dpi(kMarginDip, dpi);
        int const padding = Scale_for_dpi(kPaddingDip, dpi);
        int const accent_bar_width = Scale_for_dpi(kAccentBarWidthDip, dpi);
        int const header_gap = Scale_for_dpi(kHeaderGapDip, dpi);
        int const icon_size = Scale_for_dpi(kIconDip, dpi);
        int const icon_gap = Scale_for_dpi(kIconGapDip, dpi);
        int const title_app_icon_size = Scale_for_dpi(kTitleAppIconDip, dpi);
        int const title_app_icon_gap = Scale_for_dpi(kTitleAppIconGapDip, dpi);
        int const title_icon_reserved = title_app_icon_size + title_app_icon_gap;
        int const min_height = Scale_for_dpi(kMinHeightDip, dpi);
        int const max_height = Scale_for_dpi(kMaxHeightDip, dpi);
        int const thumbnail_max_height = Scale_for_dpi(kThumbnailMaxHeightDip, dpi);
        int const thumbnail_gap = Scale_for_dpi(kThumbnailGapDip, dpi);

        int const content_left = accent_bar_width + padding;
        int const content_right = width - padding;
        int const content_width = content_right - content_left;
        int const title_left = content_left;
        int title_right = content_right - title_icon_reserved;
        if (title_right <= title_left) {
            title_right = title_left + 1;
        }
        int const text_left = content_left + icon_size + icon_gap;
        int const text_width = std::max(1, content_right - text_left);

        int title_height = title_app_icon_size;
        int body_height = Scale_for_dpi(kFallbackTextHeightDip, dpi);

        HDC const hdc = GetDC(hwnd_);
        if (hdc != nullptr) {
            HGDIOBJ const old_font = SelectObject(
                hdc, title_font_ ? title_font_ : GetStockObject(DEFAULT_GUI_FONT));

            RECT title_measure{};
            title_measure.right = title_right - title_left;
            DrawTextW(hdc, kTitleText, -1, &title_measure,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS |
                          DT_CALCRECT);
            int const measured_title = title_measure.bottom - title_measure.top;
            if (measured_title > title_height) {
                title_height = measured_title;
            }

            SelectObject(hdc,
                         body_font_ ? body_font_ : GetStockObject(DEFAULT_GUI_FONT));
            RECT body_measure{};
            body_measure.right = text_width;
            DrawTextW(hdc, message_.c_str(), -1, &body_measure,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            int const measured_body = body_measure.bottom - body_measure.top;
            if (measured_body > 0) {
                body_height = measured_body;
            }

            SelectObject(hdc, old_font);
            ReleaseDC(hwnd_, hdc);
        }

        int const body_row_height = std::max(icon_size, body_height);
        int height = padding + title_height + header_gap + body_row_height + padding;

        if (thumbnail_ != nullptr && thumbnail_width_ > 0 && thumbnail_height_ > 0) {
            float const scale_w = static_cast<float>(content_width) /
                                  static_cast<float>(thumbnail_width_);
            float const scale_h = static_cast<float>(thumbnail_max_height) /
                                  static_cast<float>(thumbnail_height_);
            float scale = (std::min)(scale_w, scale_h);
            if (scale > 1.0f) {
                scale = 1.0f;
            }
            height += thumbnail_gap +
                      static_cast<int>(static_cast<float>(thumbnail_height_) * scale);
        }

        height = std::clamp(height, min_height, max_height);

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

        int const x = work_area.right - width - margin;
        int const y = work_area.bottom - height - margin;
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        InvalidateRect(hwnd_, nullptr, TRUE);
        KillTimer(hwnd_, kTimerId);
        SetTimer(hwnd_, kTimerId, kDurationMs, nullptr);
    }

    void Destroy() {
        if (!Is_open()) {
            return;
        }
        KillTimer(hwnd_, kTimerId);
        DestroyWindow(hwnd_);
    }

  private:
    static constexpr UINT_PTR kTimerId = 1;
    static constexpr UINT kDurationMs = 5000;
    static constexpr int kWidthDip = 340;
    static constexpr int kMarginDip = 18;
    static constexpr int kPaddingDip = 14;
    static constexpr int kAccentBarWidthDip = 4;
    static constexpr int kHeaderGapDip = 6;
    static constexpr int kIconDip = 20;
    static constexpr int kIconGapDip = 10;
    static constexpr int kTitleAppIconDip = 14;
    static constexpr int kTitleAppIconGapDip = 6;
    static constexpr int kTitleFontDip = 12;
    static constexpr int kBodyFontDip = 13;
    static constexpr int kMinHeightDip = 56;
    static constexpr int kMaxHeightDip = 280;
    static constexpr int kFallbackTextHeightDip = 18;
    static constexpr int kThumbnailMaxHeightDip = 80;
    static constexpr int kThumbnailGapDip = 8;
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
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
        case WM_NCRBUTTONDOWN:
            KillTimer(hwnd_, kTimerId);
            Hide();
            return 0;
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

                int const padding = Scale_for_dpi(kPaddingDip, dpi);
                int const accent_bar_width = Scale_for_dpi(kAccentBarWidthDip, dpi);
                int const header_gap = Scale_for_dpi(kHeaderGapDip, dpi);
                int const icon_size = Scale_for_dpi(kIconDip, dpi);
                int const icon_gap = Scale_for_dpi(kIconGapDip, dpi);
                int const title_app_icon_size = Scale_for_dpi(kTitleAppIconDip, dpi);
                int const title_app_icon_gap = Scale_for_dpi(kTitleAppIconGapDip, dpi);
                int const title_icon_reserved =
                    title_app_icon_size + title_app_icon_gap;
                int const thumbnail_max_height =
                    Scale_for_dpi(kThumbnailMaxHeightDip, dpi);
                int const thumbnail_gap = Scale_for_dpi(kThumbnailGapDip, dpi);

                COLORREF const background_color = winui::kToastBackground;
                COLORREF const border_color = winui::kToastBorder;
                COLORREF const title_color = winui::kToastTitleText;
                COLORREF const text_color = winui::kToastBodyText;
                COLORREF const accent_color = Toast_accent_color(icon_);

                int const content_left = accent_bar_width + padding;
                int const content_right = client.right - padding;
                int const content_width = content_right - content_left;

                HBRUSH const bg_brush = CreateSolidBrush(background_color);
                if (bg_brush != nullptr) {
                    FillRect(hdc, &client, bg_brush);
                    DeleteObject(bg_brush);
                }

                RECT accent_bar = client;
                accent_bar.right = accent_bar.left + accent_bar_width;
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

                int const title_left = content_left;
                int title_right = content_right - title_icon_reserved;
                if (title_right <= title_left) {
                    title_right = title_left + 1;
                }

                RECT title_rect{};
                title_rect.left = title_left;
                title_rect.top = padding;
                title_rect.right = title_right;
                title_rect.bottom = title_rect.top + title_app_icon_size;

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, title_color);
                HGDIOBJ const old_font = SelectObject(
                    hdc, title_font_ ? title_font_ : GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(hdc, kTitleText, -1, &title_rect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
                              DT_END_ELLIPSIS);

                HICON loaded_app_icon = static_cast<HICON>(LoadImageW(
                    hinstance_, MAKEINTRESOURCEW(kAppIconResourceId), IMAGE_ICON,
                    title_app_icon_size, title_app_icon_size, LR_DEFAULTCOLOR));
                if (loaded_app_icon != nullptr) {
                    int const app_icon_x = content_right - title_app_icon_size;
                    int const app_icon_y =
                        title_rect.top +
                        ((title_rect.bottom - title_rect.top - title_app_icon_size) /
                         2);
                    DrawIconEx(hdc, app_icon_x, app_icon_y, loaded_app_icon,
                               title_app_icon_size, title_app_icon_size, 0, nullptr,
                               DI_NORMAL);
                    DestroyIcon(loaded_app_icon);
                }

                int const body_top = title_rect.bottom + header_gap;

                Draw_severity_icon(hdc, content_left, body_top, icon_size, icon_);

                int const text_left = content_left + icon_size + icon_gap;
                RECT body_rect{};
                body_rect.left = text_left;
                body_rect.top = body_top;
                body_rect.right = content_right;
                body_rect.bottom = client.bottom - padding;

                SetTextColor(hdc, text_color);
                SelectObject(hdc, body_font_ ? body_font_
                                             : GetStockObject(DEFAULT_GUI_FONT));

                RECT body_measure = body_rect;
                DrawTextW(hdc, message_.c_str(), -1, &body_measure,
                          DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
                int const body_text_bottom = body_measure.bottom;

                DrawTextW(hdc, message_.c_str(), -1, &body_rect,
                          DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

                if (thumbnail_ != nullptr && thumbnail_width_ > 0 &&
                    thumbnail_height_ > 0) {

                    float const scale_w = static_cast<float>(content_width) /
                                          static_cast<float>(thumbnail_width_);
                    float const scale_h = static_cast<float>(thumbnail_max_height) /
                                          static_cast<float>(thumbnail_height_);
                    float scale = (std::min)(scale_w, scale_h);
                    if (scale > 1.0f) {
                        scale = 1.0f;
                    }
                    int const thumb_w =
                        static_cast<int>(static_cast<float>(thumbnail_width_) * scale);
                    int const thumb_h =
                        static_cast<int>(static_cast<float>(thumbnail_height_) * scale);
                    int const thumb_top =
                        std::max(body_text_bottom, body_top + icon_size) +
                        thumbnail_gap;
                    int const thumb_left = content_left;

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

                SelectObject(hdc, old_font);
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
    HBITMAP thumbnail_ = nullptr;
    TrayBalloonIcon icon_ = TrayBalloonIcon::Info;
    int thumbnail_width_ = 0;
    int thumbnail_height_ = 0;
};

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

bool TrayWindow::Create(HINSTANCE hinstance, bool enable_testing_hotkeys) {
    if (Is_open()) {
        return true;
    }
    hinstance_ = hinstance;
    testing_hotkeys_enabled_ = enable_testing_hotkeys;
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
        notify_data.szTip[kTip.size()] = L'\0';
    }
    if (!Shell_NotifyIconW(NIM_ADD, &notify_data)) {
        DestroyWindow(hwnd);
        return false;
    }

    toast_popup_ = std::make_unique<ToastPopup>(hinstance_);

    for (;;) {
        if (RegisterHotKey(hwnd, HotkeyStartCapture, kModNoRepeat, VK_SNAPSHOT)) {
            break;
        }
        int const user_choice =
            MessageBoxW(hwnd,
                        L"Print Screen could not be registered. It may be in "
                        L"use by another program.\n"
                        L"You can still start capture from the tray menu.",
                        L"Greenflame", MB_ABORTRETRYIGNORE | MB_ICONWARNING);
        if (user_choice == IDRETRY) {
            continue;
        }
        break;
    }

    bool const copy_window_registered =
        RegisterHotKey(hwnd, HotkeyCopyWindow,
                       static_cast<UINT>(MOD_CONTROL | kModNoRepeat), VK_SNAPSHOT) != 0;
    bool const copy_monitor_registered =
        RegisterHotKey(hwnd, HotkeyCopyMonitor,
                       static_cast<UINT>(MOD_SHIFT | kModNoRepeat), VK_SNAPSHOT) != 0;
    bool const copy_desktop_registered =
        RegisterHotKey(hwnd, HotkeyCopyDesktop,
                       static_cast<UINT>(MOD_CONTROL | MOD_SHIFT | kModNoRepeat),
                       VK_SNAPSHOT) != 0;
    bool const copy_last_region_registered =
        RegisterHotKey(hwnd, HotkeyCopyLastRegion,
                       static_cast<UINT>(MOD_ALT | kModNoRepeat), VK_SNAPSHOT) != 0;
    bool const copy_last_window_registered =
        RegisterHotKey(hwnd, HotkeyCopyLastWindow,
                       static_cast<UINT>(MOD_CONTROL | MOD_ALT | kModNoRepeat),
                       VK_SNAPSHOT) != 0;
    if (!copy_window_registered || !copy_monitor_registered ||
        !copy_desktop_registered || !copy_last_region_registered ||
        !copy_last_window_registered) {
        MessageBoxW(hwnd,
                    L"One or more modified Print Screen hotkeys could not be "
                    L"registered.\n"
                    L"You can still use these commands from the tray menu.",
                    L"Greenflame", MB_OK | MB_ICONWARNING);
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
                              HBITMAP thumbnail) {
    if (!Is_open() || !message || message[0] == L'\0') {
        if (thumbnail != nullptr) {
            DeleteObject(thumbnail);
        }
        return;
    }
    if (!toast_popup_) {
        toast_popup_ = std::make_unique<ToastPopup>(hinstance_);
    }
    toast_popup_->Show(icon, message, thumbnail);
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
        int const command = LOWORD(wparam);
        if (command == StartCapture) {
            Notify_start_capture();
        } else if (command == CopyWindow) {
            PostMessage(hwnd_, kDeferredCopyWindowMessage, 0, 0);
        } else if (command == CopyMonitor) {
            Notify_copy_monitor_to_clipboard();
        } else if (command == CopyDesktop) {
            Notify_copy_desktop_to_clipboard();
        } else if (command == CopyLastRegion) {
            Notify_copy_last_region_to_clipboard();
        } else if (command == CopyLastWindow) {
            Notify_copy_last_window_to_clipboard();
        } else if (command == Exit) {
            if (events_) {
                events_->On_exit_requested();
            }
        }
        return 0;
    }
    case WM_HOTKEY:
        if (wparam == HotkeyStartCapture) {
            Notify_start_capture();
        } else if (wparam == HotkeyCopyWindow) {
            Notify_copy_window_to_clipboard();
        } else if (wparam == HotkeyCopyMonitor) {
            Notify_copy_monitor_to_clipboard();
        } else if (wparam == HotkeyCopyDesktop) {
            Notify_copy_desktop_to_clipboard();
        } else if (wparam == HotkeyCopyLastRegion) {
            Notify_copy_last_region_to_clipboard();
        } else if (wparam == HotkeyCopyLastWindow) {
            Notify_copy_last_window_to_clipboard();
        }
#ifdef DEBUG
        else if (testing_hotkeys_enabled_ && wparam == HotkeyTestingError) {
            Show_balloon(TrayBalloonIcon::Error, kTestingErrorBalloonMessage);
        } else if (testing_hotkeys_enabled_ && wparam == HotkeyTestingWarning) {
            Show_balloon(TrayBalloonIcon::Warning, kTestingWarningBalloonMessage);
        }
#endif
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
    AppendMenuW(menu, MF_STRING, Exit, L"Exit");
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd_);
    TrackPopupMenuEx(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, cursor.x,
                     cursor.y, hwnd_, nullptr);
    DestroyMenu(menu);
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

} // namespace greenflame
